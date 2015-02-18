#include "test.h"

#define TEST_CONFIG_FILE "t/tmp/bolo.cfg"
#define TEST_SAVE_FILE   "t/tmp/save"

TESTS {
	mkdir("t/tmp", 0755);

	alarm(5);
	server_t svr;
	int rc;
	void *client, *sub;
	pthread_t tid;

	memset(&svr, 0, sizeof(svr));
	write_file(TEST_CONFIG_FILE,
		"broadcast inproc://bcast\n"
		"savefile  " TEST_SAVE_FILE "\n"
		"dumpfiles t/tmp/dump.\%s\n"
		""
		"type :default {\n"
		"  freshness 60\n"
		"  warning \"it is stale\"\n"
		"}\n"
		"use :default\n"
		""
		"state test.state.0\n"
		"state test.state.1\n"
		""
		"window @minutely 60\n"
		"window @hourly   3600\n"
		""
		"counter @minutely counter1\n"
		"sample  @hourly   res.df:/\n"
		"", 0);
	write_file(TEST_SAVE_FILE,
		"BOLO\0\1\0\0T\x92J\x97\0\0\0\4"                     /* 16 */
		"\0'\0\1T\x92=[\2\0test.state.1\0critically-ness\0"  /* 39 */
		"\0'\0\1T\x92=[\1\0test.state.0\0its problematic\0"  /* 39 */
		"\0\x19\0\2T\x92=[\0\0\0\0\0\0\0\0counter1\0"        /* 25 */
		"\0Q\0\3T\x92=[\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		              "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		              "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		              "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		              "res.df:/\0"                           /* 81 */
		"\0\0", 16 + 39 + 39 + 25 + 81 + 2);

	CHECK(configure("t/tmp/bolo.cfg", &svr) == 0,
		"failed to read configuration file " TEST_CONFIG_FILE);
	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(pthread_create(&tid, NULL, kernel, &svr) == 0,
		"failed to spin up kernel thread");
	sleep_ms(50);
	CHECK(client = zmq_socket(svr.zmq, ZMQ_DEALER),
		"failed to create mock kernel test socket");
	CHECK(zmq_connect(client, KERNEL_ENDPOINT) == 0,
		"failed to connect to kernel socket");
	CHECK(sub = zmq_socket(svr.zmq, ZMQ_SUB),
		"failed to create mock kernel subscriber socket");
	CHECK(zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0) == 0,
		"failed to set ZMQ_SUBSCRIBE option to '' on kernel subscriber socket");
	CHECK(zmq_connect(sub, svr.config.broadcast) == 0,
		"failed to connect to kernel publisher socket");

	/* ----------------------------- */

	pdu_t *p;
	uint32_t time = time_s();
	char *ts = string("%u", time);
	char *s;

	/* send an invalid PDU */
	p = pdu_make("@INVALID!", 2, "foo", "bar");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [@INVALID!] PDU to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "ERROR", "kernel replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "Invalid PDU", "Error message returned"); free(s);
	pdu_free(p);

	/* get the state of test.state.0 */
	p = pdu_make("GET.STATE", 1, "test.state.0");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [GET.STATE] PDU to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "STATE", "kernel replied with a [STATE]");
	is_string(s = pdu_string(p, 1), "test.state.0",    "STATE[0] is state name"); free(s);
	is_string(s = pdu_string(p, 2), "1418870107",      "STATE[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "fresh",           "STATE[2] is freshness boolean"); free(s);
	is_string(s = pdu_string(p, 4), "WARNING",         "STATE[3] is status"); free(s);
	is_string(s = pdu_string(p, 5), "its problematic", "STATE[4] is summary"); free(s);
	pdu_free(p);

	/* send test.state.3 (not configured) initial ok */
	p = pdu_make("PUT.STATE", 4, ts, "test.state.3", "0", "NEW");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [PUT.STATE] PDU to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "ERROR", "kernel replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "State Not Found", "Error message returned"); free(s);
	pdu_free(p);


	/* send test.state.0 recovery */
	p = pdu_make("PUT.STATE", 4, ts, "test.state.0", "0", "all good");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [PUT.STATE] PDU to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with an [OK]");
	pdu_free(p);

	/* check the publisher pipeline */
	p = pdu_recv(sub);
	is_string(pdu_type(p), "EVENT", "kernel broadcast an [EVENT] PDU");
	is_string(s = pdu_string(p, 1), "test.state.0", "EVENT[0] is state name");   free(s);
	is_string(s = pdu_string(p, 2), ts,             "EVENT[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "fresh",        "EVENT[2] is freshness");    free(s);
	is_string(s = pdu_string(p, 4), "OK",           "EVENT[3] is status");       free(s);
	is_string(s = pdu_string(p, 5), "all good",     "EVENT[4] is summary");      free(s);
	pdu_free(p);

	p = pdu_recv(sub);
	is_string(pdu_type(p), "STATE", "kernel broadcast a [STATE] PDU");
	is_string(s = pdu_string(p, 1), "test.state.0", "STATE[0] is state name");   free(s);
	is_string(s = pdu_string(p, 2), ts,             "STATE[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "fresh",        "STATE[2] is freshness");    free(s);
	is_string(s = pdu_string(p, 4), "OK",           "STATE[3] is status");       free(s);
	is_string(s = pdu_string(p, 5), "all good",     "STATE[4] is summary");      free(s);
	pdu_free(p);

	/* send test.state.1 continuing crit */
	p = pdu_make("PUT.STATE", 4, ts, "test.state.1", "2", "critically-ness");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent 2nd [PUT.STATE] PDU to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with an [OK]");
	pdu_free(p);

	/* check the publisher pipeline */

	/* no EVENT pdu, since its an ongoing state */

	p = pdu_recv(sub);
	is_string(pdu_type(p), "STATE", "kernel broadcast a [STATE] PDU");
	is_string(s = pdu_string(p, 1), "test.state.1",    "STATE[0] is state name");   free(s);
	is_string(s = pdu_string(p, 2), ts,                "STATE[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "fresh",           "STATE[2] is freshness");    free(s);
	is_string(s = pdu_string(p, 4), "CRITICAL",        "STATE[3] is status");       free(s);
	is_string(s = pdu_string(p, 5), "critically-ness", "STATE[4] is summary");      free(s);
	pdu_free(p);

	/* dump the state file (to /t/tmp/dump.test) */
	unlink("t/tmp/dump.test");
	p = pdu_make("DUMP", 1, "test");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [DUMP] PDU to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "DUMP", "kernel replied with a [DUMP]");
	is_string(s = pdu_string(p, 1), "t/tmp/dump.test", "[DUMP] reply returned file path"); free(s);
	pdu_free(p);

	file_is("t/tmp/dump.test",
		s = string("---\n"
		           "# generated by bolo\n"
		           "test.state.0:\n"
		           "  status:    OK\n"
		           "  message:   all good\n"
		           "  last_seen: %s\n"
		           "  fresh:     yes\n"
		           "test.state.1:\n"
		           "  status:    CRITICAL\n"
		           "  message:   critically-ness\n"
		           "  last_seen: %s\n"
		           "  fresh:     yes\n", ts, ts),
		"dumped YAML file"); free(s);

	/* get state of test.state.1 via [STATE] */
	p = pdu_make("GET.STATE", 1, "test.state.1");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [GET.STATE] query to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "STATE", "kernel replied with a [STATE]");
	is_string(s = pdu_string(p, 1), "test.state.1",    "STATE[0] is state name"); free(s);
	is_string(s = pdu_string(p, 2), ts,                "STATE[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "fresh",           "STATE[2] is freshness boolean"); free(s);
	is_string(s = pdu_string(p, 4), "CRITICAL",        "STATE[3] is status"); free(s);
	is_string(s = pdu_string(p, 5), "critically-ness", "STATE[4] is summary"); free(s);
	pdu_free(p);

	/* get non-existent state via [STATE] */
	p = pdu_make("GET.STATE", 1, "fail.enoent//0");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [GET.STATE] query to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "ERROR", "kernel replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "State Not Found", "Error message returned"); free(s);
	pdu_free(p);

	/* increment counter counter1 value a few times */
	p = pdu_make("PUT.COUNTER", 3, ts, "counter1", "1");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [PUT.COUNTER] to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with [OK]");
	pdu_free(p);

	p = pdu_make("PUT.COUNTER", 3, ts, "counter1", "4");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent second [PUT.COUNTER] to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with [OK]");
	pdu_free(p);

	/* add samples to res.df:/ */
	p = pdu_make("PUT.SAMPLE", 3, ts, "res.df:/", "42");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [PUT.SAMPLE] to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with [OK]");
	pdu_free(p);

	/* save state (to /t/tmp/save) */
	p = pdu_make("SAVESTATE", 0);
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [SAVESTATE] PDU to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with a [SAVESTATE]");
	pdu_free(p);

	s = calloc(195, sizeof(char));
	memcpy(s, "BOLO"     /* H:magic      +4    0 */
	          "\0\1"     /* H:version    +2    4 */
	          "\0\0"     /* H:flags      +2    6 */
	          "...."     /* H:timestamp  +4    8 (to be filled in later) */
	          "\0\0\0\4" /* H:count      +4   12 */              /* +16 */

	      /* STATES */
	          "\0\x20"   /* 0:len        +2   16 */
	          "\0\1"     /* 0:flags      +2   18 */
	          "...."     /* 0:last_seen  +4   20 (to be filled in later) */
	          "\0"       /* 0:status     +1   24 */
	          "\0"       /* 0:stale      +1   25 */
	          "test.state.0\0"       /* +13   26 */
	          "all good\0"           /*  +9   39 */              /* +32 */

	          "\0\x27"   /* 1:len        +2   48 */
	          "\0\1"     /* 1:flags      +2   50 */
	          "...."     /* 1:last_seen  +4   52 (to be filled in later) */
	          "\2"       /* 1:status     +1   56 */
	          "\0"       /* 1:stale      +1   57 */
	          "test.state.1\0"       /* +13   58 */
	          "critically-ness\0"    /* +16   71 */              /* +39 */

	      /* COUNTERS */
	          "\0\x19"   /* 0:len        +2   87 */
	          "\0\2"     /* 0:flags      +2   89 */
	          "...."     /* 0:last_seen  +4   91 (to be filled in later) */
	          "\0\0\0\0" /* 0:value              */
	          "\0\0\0\5" /*              +8   95 */
	          "counter1\0"           /*  +9  104 */              /* +25 */

	      /* SAMPLES */
	          "\0\x51"   /* 0:len        +2  112 */
	          "\0\3"     /* 0:flags      +2  114 */
	          "...."     /* 0:last_seen  +4  116 (to be filled in later) */
	          "\0\0\0\0" /* 0:n                  */
	          "\0\0\0\1" /*              +8  120 */
	          "\0\0\0\0" /* 0:min                */
	          "\0\0\xc5\x41" /*          +8  128 */
	          "\0\0\0\0" /* 0:max                */
	          "\0\0\xc5\x41" /*          +8  136 */
	          "\0\0\0\0" /* 0:sum                */
	          "\0\0\xc5\x41" /*          +8  144 */
	          "\0\0\0\0" /* 0:mean               */
	          "\0\0\0\0" /*              +8  152 */
	          "\0\0\0\0" /* 0:mean_              */
	          "\0\0\0\0" /*              +8  160 */
	          "\0\0\0\0" /* 0:var                */
	          "\0\0\0\0" /*              +8  168 */
	          "\0\0\0\0" /* 0:var_               */
	          "\0\0\0\0" /*              +8  176 */
	          "res.df:/\0"           /*  +9  184 */              /* +81 */
                                     /*      193 */
	          "\0\0", 195);
	*(uint32_t*)(s+  8) = htonl(time);
	*(uint32_t*)(s+ 20) = htonl(time);
	*(uint32_t*)(s+ 52) = htonl(time);
	*(uint32_t*)(s+ 91) = htonl(time);
	*(uint32_t*)(s+116) = htonl(time);

	binfile_is("t/tmp/save", s, 195,
		"save file (binary)"); free(s);

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);
}
