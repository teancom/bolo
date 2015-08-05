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

#define TEST_SAVE_FILE   "t/tmp/save"
#define TEST_KEYS_FILE   "t/tmp/keys"

TESTS {
	DEBUGGING("t/match");
	NEED_FS();
	TIME_ALIGN();
	TIMEOUT(5);

	server_t *svr = CONFIGURE(
		"broadcast inproc://test.broadcast\n"
		"listener inproc://test.listener\n"
		"controller inproc://test.controller\n"
		"savefile  " TEST_SAVE_FILE "\n"
		"keysfile  " TEST_KEYS_FILE "\n"
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
		"rate    @minutely m/permin$/\n");

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

	void *zmq;
	CHECK(zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	KERNEL(zmq, svr);
	void *super  = SUPERVISOR(zmq);
	void *client = CLIENT(zmq);
	void *mgr    = MANAGER(zmq);
	void *sub    = SUBSCRIBER(zmq);

	/* ----------------------------- */

	uint32_t time = time_s();
	char *ts = string("%u", time);
	char *s;

	/* get the state of test.state.0 */
	send_ok(pdu_make("STATE", 1, "test.state.0"), mgr);
	recv_ok(mgr, "STATE", 5, "test.state.0", "1418870107", "fresh", "WARNING", "its problematic");

	/* send test.state.3 (not configured) initial ok */
	send_ok(pdu_make("STATE", 4, ts, "foo.state.3", "0", "NEW"), client);

	/* send test.state.0 recovery */
	send_ok(pdu_make("STATE", 4, ts, "test.state.0", "0", "all good"), client);

	/* check the publisher pipeline */
	recv_ok(sub, "TRANSITION", 5, "test.state.0", ts, "fresh", "OK", "all good");
	recv_ok(sub, "STATE",      5, "test.state.0", ts, "fresh", "OK", "all good");

	/* send test.state.1 continuing crit */
	send_ok(pdu_make("STATE", 4, ts, "test.state.1", "2", "critically-ness"), client);

	/* check the publisher pipeline */
	/* no TRANSITION pdu, since its an ongoing state */
	recv_ok(sub, "STATE", 5, "test.state.1", ts, "fresh", "CRITICAL", "critically-ness");

	/* dump the state file */
	send_ok(pdu_make("DUMP", 0), mgr);
	recv_ok(mgr, "DUMP", 1, s = string("---\n"
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
	                                   "  fresh:     yes\n", ts, ts));
	free(s);

	/* get state of test.st                        ate.1 via [STATE] */
	send_ok(pdu_make("STATE", 1, "test.state.1"), mgr);
	recv_ok(mgr, "STATE", 5, "test.state.1", ts, "fresh", "CRITICAL", "critically-ness");

	/* create a new counter */
	send_ok(pdu_make("COUNTER", 3, ts, "err.segfaults", "1"), client);
	send_ok(pdu_make("COUNTER", 3, ts, "err.segfaults", "3"), client);

	/* create a new sample */
	send_ok(pdu_make("SAMPLE", 3, ts, "disk:/root", "42"), client);

	/* create a new rate */
	send_ok(pdu_make("RATE", 3, ts, "req.permin", "100"), client);
	send_ok(pdu_make("RATE", 3, ts, "req.permin", "200"), client);

	/* wait for the PULL pipeline to catch up before we interleave our [SAVESTATE] */
	sleep_ms(150);

	/* save state (to /t/tmp/save) */
	send_ok(pdu_make("SAVESTATE", 0), mgr);
	recv_ok(mgr, "OK", 0);

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

	free(ts);

	/* ----------------------------- */

	pdu_send_and_free(pdu_make("TERMINATE", 0), super);
	zmq_close(super);
	zmq_close(client);
	zmq_close(mgr);
	zmq_close(sub);
	zmq_ctx_destroy(zmq);
	done_testing();
}