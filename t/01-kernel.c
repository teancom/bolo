#include "test.h"

TESTS {
	alarm(5);
	server_t svr;
	int rc;
	void *db_client, *sub;
	pthread_t tid;

	mkdir("t/tmp", 0755);

	memset(&svr, 0, sizeof(svr));
	svr.config.broadcast = "inproc://bcast";
	svr.config.dumpfiles = "t/tmp/dump.%s";
	write_file(svr.config.savefile = "t/tmp/save",
		"BOLO\0\1\0\0T\x92J\x97\0\0\0\2"                     /* 16 */
		"\0'\0\1T\x92=[\2\0test.state.1\0critically-ness\0"  /* 39 */
		"\0'\0\1T\x92=[\1\0test.state.0\0its problematic\0"  /* 39 */
		"\0\0", 94 + 2);
	type_t type_default = {
		.freshness = 60,
		.status    = WARNING,
		.summary   = "it is stale",
	};
	state_t state0 = { .type = &type_default, .name = "test.state.0" };
	state_t state1 = { .type = &type_default, .name = "test.state.1" };
	hash_set(&svr.db.states, "test.state.0", &state0);
	hash_set(&svr.db.states, "test.state.1", &state1);

	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(pthread_create(&tid, NULL, kernel, &svr) == 0,
		"failed to spin up kernel thread");
	sleep_ms(50);
	CHECK(db_client = zmq_socket(svr.zmq, ZMQ_DEALER),
		"failed to create mock kernel test socket");
	CHECK(zmq_connect(db_client, KERNEL_ENDPOINT) == 0,
		"failed to connect to kernel socket");
	CHECK(sub = zmq_socket(svr.zmq, ZMQ_SUB),
		"failed to create mock db subscriber socket");
	CHECK(zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0) == 0,
		"failed to set ZMQ_SUBSCRIBE option to '' on db subscriber socket");
	CHECK(zmq_connect(sub, svr.config.broadcast) == 0,
		"failed to connect to db publisher socket");

	/* ----------------------------- */

	pdu_t *p;
	uint32_t time = time_s();
	char *ts = string("%u", time);
	char *s;

	/* send an invalid PDU */
	p = pdu_make("@INVALID!", 2, "foo", "bar");
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent [@INVALID!] PDU to kernel");

	p = pdu_recv(db_client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "ERROR", "kernel replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "Invalid PDU", "Error message returned"); free(s);
	pdu_free(p);

	/* get the state of test.state.0 */
	p = pdu_make("GET.STATE", 1, "test.state.0");
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent [GET.STATE] PDU to kernel");

	p = pdu_recv(db_client);
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
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent [PUT.STATE] PDU to kernel");

	p = pdu_recv(db_client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "ERROR", "kernel replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "State Not Found", "Error message returned"); free(s);
	pdu_free(p);


	/* send test.state.0 initial ok */
	p = pdu_make("PUT.STATE", 4, ts, "test.state.0", "0", "all good");
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent [PUT.STATE] PDU to kernel");

	p = pdu_recv(db_client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with an [OK]");
	pdu_free(p);

	/* check the publisher pipeline */
	p = pdu_recv(sub);
	is_string(pdu_type(p), "STATE", "db broadcast a [STATE] PDU");
	is_string(s = pdu_string(p, 1), "test.state.0", "STATE[0] is state name");   free(s);
	is_string(s = pdu_string(p, 2), ts,             "STATE[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "fresh",        "STATE[2] is freshness");    free(s);
	is_string(s = pdu_string(p, 4), "OK",           "STATE[3] is status");       free(s);
	is_string(s = pdu_string(p, 5), "all good",     "STATE[4] is summary");      free(s);
	pdu_free(p);

	/* send test.state.1 initial crit */
	p = pdu_make("PUT.STATE", 4, ts, "test.state.1", "2", "critically-ness");
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent 2nd [PUT.STATE] PDU to kernel");

	p = pdu_recv(db_client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with an [OK]");
	pdu_free(p);

	/* check the publisher pipeline */
	p = pdu_recv(sub);
	is_string(pdu_type(p), "STATE", "db broadcast a [STATE] PDU");
	is_string(s = pdu_string(p, 1), "test.state.1",    "STATE[0] is state name");   free(s);
	is_string(s = pdu_string(p, 2), ts,                "STATE[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "fresh",           "STATE[2] is freshness");    free(s);
	is_string(s = pdu_string(p, 4), "CRITICAL",        "STATE[3] is status");       free(s);
	is_string(s = pdu_string(p, 5), "critically-ness", "STATE[4] is summary");      free(s);
	pdu_free(p);

	/* dump the state file (to /t/tmp/dump.test) */
	unlink("t/tmp/dump.test");
	p = pdu_make("DUMP", 1, "test");
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent [DUMP] PDU to kernel");

	p = pdu_recv(db_client);
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
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent [GET.STATE] query to kernel");

	p = pdu_recv(db_client);
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
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent [GET.STATE] query to kernel");

	p = pdu_recv(db_client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "ERROR", "kernel replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "State Not Found", "Error message returned"); free(s);
	pdu_free(p);

	/* save state (to /t/tmp/save) */
	p = pdu_make("SAVESTATE", 1, "test");
	rc = pdu_send_and_free(p, db_client);
	is_int(rc, 0, "sent [SAVESTATE] PDU to kernel");

	p = pdu_recv(db_client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with a [SAVESTATE]");
	pdu_free(p);

	s = calloc(87, sizeof(char));
	memcpy(s, "BOLO"     /* H:magic      +4 */
	          "\0\1"     /* H:version    +2 */
	          "\0\0"     /* H:flags      +2 */
	          "...."     /* H:timestamp  +4 (to be filled in later) */
	          "\0\0\0\2" /* H:count      +4 */              /* +16 */

	          "\0\x20"   /* 0:len        +2 */
	          "\0\1"     /* 0:flags      +2 */
	          "...."     /* 0:last_seen  +4 (to be filled in later) */
	          "\0"       /* 0:status     +1 */
	          "\0"       /* 0:stale      +1 */
	          "test.state.0\0"       /* +13 */
	          "all good\0"           /*  +9 */              /* +32 */

	          "\0\x27"   /* 1:len        +2 */
	          "\0\1"     /* 1:flags      +2 */
	          "...."     /* 1:last_seen  +4 (to be filled in later) */
	          "\2"       /* 1:status     +1 */
	          "\0"       /* 1:stale      +1 */
	          "test.state.1\0"       /* +13 */
	          "critically-ness\0"    /* +16 */              /* +39 */
	          "", 87);
	*(uint32_t*)(s+4+2+2)     = htonl(time);
	*(uint32_t*)(s+16+2+2)    = htonl(time);
	*(uint32_t*)(s+16+32+2+2) = htonl(time);

	binfile_is("t/tmp/save", s, 87,
		"save file (binary)"); free(s);

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);
}
