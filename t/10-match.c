/*
  Copyright 2015 James Hunt <james@jameshunt.us>

  This file is part of Bolo.

  Bolo is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.

  Bolo is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along
  with Bolo.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "test.h"

#define TEST_CONFIG_FILE "t/tmp/bolo.cfg"
#define TEST_SAVE_FILE   "t/tmp/save"
#define TEST_KEYS_FILE   "t/tmp/keys"

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
		"keysfile  " TEST_KEYS_FILE "\n"
		"dumpfiles t/tmp/dump.\%s\n"
		""
		"type :default {\n"
		"  freshness 60\n"
		"  warning \"it is stale\"\n"
		"}\n"
		"use :default\n"
		"state m/digit\\d+/\n"
		"state m/^test/\n"
		"\n"
		"window @minutely 60\n"
		"counter @minutely m/^err\\./\n"
		"sample  3600      m/^disk:/\n"
		"rate    @minutely m/permin$/\n"
		"", 0);
	write_file(TEST_SAVE_FILE,
		"BOLO\0\1\0\0T\x92J\x97\0\0\0\5"                     /* 16 */
		"\0'\0\1T\x92=[\2\0test.state.1\0critically-ness\0"  /* 39 */
		"\0'\0\1T\x92=[\1\0test.state.0\0its problematic\0"  /* 39 */
		"\0'\0\1T\x92=[\2\0skip.state.3\0critically-ness\0"  /* 39 */
		"\0\x19\0\2T\x92=[\0\0\0\0\0\0\0\0counter1\0"        /* 25 */
		"\0Q\0\3T\x92=[\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		              "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		              "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		              "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
		              "res.df:/\0"                           /* 81 */
		"\0\0", 16 + 39 + 39 + 39 + 25 + 81 + 2);
	unlink(TEST_KEYS_FILE);

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
	p = pdu_make("PUT.STATE", 4, ts, "foo.state.3", "0", "NEW");
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
	is_string(pdu_type(p), "TRANSITION", "kernel broadcast an [TRANSITION] PDU");
	is_string(s = pdu_string(p, 1), "test.state.0", "TRANSITION[0] is state name");   free(s);
	is_string(s = pdu_string(p, 2), ts,             "TRANSITION[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "fresh",        "TRANSITION[2] is freshness");    free(s);
	is_string(s = pdu_string(p, 4), "OK",           "TRANSITION[3] is status");       free(s);
	is_string(s = pdu_string(p, 5), "all good",     "TRANSITION[4] is summary");      free(s);
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

	/* no TRANSITION pdu, since its an ongoing state */

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

	/* create a new counter */
	p = pdu_make("PUT.COUNTER", 3, ts, "err.segfaults", "1");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [PUT.COUNTER] to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with [OK]");
	pdu_free(p);

	p = pdu_make("PUT.COUNTER", 3, ts, "err.segfaults", "3");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent second [PUT.COUNTER] to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with [OK]");
	pdu_free(p);

	/* create a new sample */
	p = pdu_make("PUT.SAMPLE", 3, ts, "disk:/root", "42");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [PUT.SAMPLE] to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with [OK]");
	pdu_free(p);

	/* create a new rate */
	p = pdu_make("PUT.RATE", 3, ts, "req.permin", "100");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent [PUT.RATE] to kernel");

	p = pdu_recv(client);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with [OK]");
	pdu_free(p);

	p = pdu_make("PUT.RATE", 3, ts, "req.permin", "200");
	rc = pdu_send_and_free(p, client);
	is_int(rc, 0, "sent 2nd [PUT.RATE] to kernel");

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

	s = calloc(241, sizeof(char));
	memcpy(s, "BOLO"     /* H:magic      +4    0 */
	          "\0\1"     /* H:version    +2    4 */
	          "\0\0"     /* H:flags      +2    6 */
	          "...."     /* H:timestamp  +4    8 (to be filled in later) */
	          "\0\0\0\5" /* H:count      +4   12 */              /* +16 */

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
	          "\0\x1e"   /* 0:len        +2   87 */
	          "\0\2"     /* 0:flags      +2   89 */
	          "...."     /* 0:last_seen  +4   91 (to be filled in later) */
	          "\0\0\0\0" /* 0:value              */
	          "\0\0\0\4" /*              +8   95 */
	          "err.segfaults\0"     /*  +14  103 */              /* +30 */

	      /* SAMPLES */
	          "\0\x53"   /* 0:len        +2  117 */
	          "\0\3"     /* 0:flags      +2  119 */
	          "...."     /* 0:last_seen  +4  121 (to be filled in later) */
	          "\0\0\0\0" /* 0:n                  */
	          "\0\0\0\1" /*              +8  125 */
	          "\0\0\0\0" /* 0:min                */
	          "\0\0\xc5\x41" /*          +8  133 */
	          "\0\0\0\0" /* 0:max                */
	          "\0\0\xc5\x41" /*          +8  141 */
	          "\0\0\0\0" /* 0:sum                */
	          "\0\0\xc5\x41" /*          +8  149 */
	          "\0\0\0\0" /* 0:mean               */
	          "\0\0\xc5\x41" /*          +8  157 */
	          "\0\0\0\0" /* 0:mean_              */
	          "\0\0\0\0" /*              +8  165 */
	          "\0\0\0\0" /* 0:var                */
	          "\0\0\0\0" /*              +8  173 */
	          "\0\0\0\0" /* 0:var_               */
	          "\0\0\0\0" /*              +8  181 */
	          "disk:/root\0"         /* +11  189 */              /* +83 */

	      /* RATES */
	          "\0\x27"   /* 0:len         +2  200 */
	          "\0\5"     /* 0:flags       +2  202 */
	          "...."     /* 0:first_seen  +4  204 */
	          "...."     /* 0:last_seen   +4  208 */
	          "\0\0\0\0" /* 0:first               */
	          "\0\0\0\x64" /*             +8  212 */
	          "\0\0\0\0" /* 0:last                */
	          "\0\0\0\xc8" /*             +8  220 */
	          "req.permin\0"          /* +11  228 */             /* +39 */
	                                       /* 239 */

	          "\0\0", 241);
	*(uint32_t*)(s+   8) = htonl(time);
	*(uint32_t*)(s+  20) = htonl(time);
	*(uint32_t*)(s+  52) = htonl(time);
	*(uint32_t*)(s+  91) = htonl(time);
	*(uint32_t*)(s+ 121) = htonl(time);
	*(uint32_t*)(s+ 204) = htonl(time);
	*(uint32_t*)(s+ 208) = htonl(time);

	binfile_is("t/tmp/save", s, 241,
		"save file (binary)"); free(s);

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);
}
