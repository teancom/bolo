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
#define SUBSCRIBER_ENDPOINT "tcp://127.0.0.1:8898"

TESTS {
	mkdir("t/tmp", 0755);

	alarm(5);
	server_t svr; memset(&svr, 0, sizeof(svr));

	write_file("t/tmp/config",
		"# sample test config\n"
		"broadcast " SUBSCRIBER_ENDPOINT "\n"
		"dumpfiles t/tmp/dump.\%s\n"
		"savefile  t/tmp/save\n"
		"keysfile  t/tmp/keys\n"
		"type :local { freshness 1 critical \"no results!!!\"\n}\n"
		"use :local\n"
		"state test.state.0\n"
		"state test.state.1\n"
		"", 0);
	CHECK(configure("t/tmp/config", &svr) == 0,
		"failed to configure bolo");

	write_file(svr.config.savefile,
		"BOLO\0\1\0\0T\x92J\x97\0\0\0\2"
		"\0'\0\1T\x92=[\2\0test.state.1\0critically-ness\0"
		"\0'\0\1T\x92=[\1\0test.state.0\0its problematic\0"
		"\0\0", 96);
	/* there is no keysfile */
	unlink(svr.config.keysfile);

	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");

	pthread_t tid;
	CHECK(pthread_create(&tid, NULL, kernel, &svr) == 0,
		"failed to spin up kernel thread");

	void *z, *sub;
	CHECK(z = zmq_socket(svr.zmq, ZMQ_DEALER),
		"failed to create mock kernel test socket");
	CHECK(zmq_connect(z, KERNEL_ENDPOINT) == 0,
		"failed to connect to kernel socket");
	CHECK(sub = zmq_socket(svr.zmq, ZMQ_SUB),
		"failed to create kernel subscriber socket");
	CHECK(zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0) == 0,
		"failed to set subscriber filter");
	CHECK(zmq_connect(sub, SUBSCRIBER_ENDPOINT) == 0,
		"failed to connect to broadcast endpoint");
	sleep_ms(50);

	/* ----------------------------- */

	char *s;
	pdu_t *q, *a;

	/* check (pre-sweep) status */
	q = pdu_make("GET.STATE", 1, "test.state.0");
	is_int(pdu_send_and_free(q, z), 0, "asked kernel for test.state.0");
	CHECK(a = pdu_recv(z), "failed to get reply from kernel");
	is_string(pdu_type(a), "STATE", "got [STATE] reply for test.state.0");
	is_string(s = pdu_string(a, 1), "test.state.0", "reply was for intended target"); free(s);
	is_string(s = pdu_string(a, 2), "1418870107", "last_seen was set from state file"); free(s);
	is_string(s = pdu_string(a, 3), "fresh", "test.state.0 is still fresh"); free(s);
	is_string(s = pdu_string(a, 4), "WARNING", "test.state.0 is WARNING"); free(s);
	is_string(s = pdu_string(a, 5), "its problematic", "summary messsage"); free(s);
	pdu_free(a);

	/* wait enough for freshness to pass */
	sleep_ms(1050);

	/* initiate freshness sweep */
	q = pdu_make("CHECKFRESH", 0);
	is_int(pdu_send_and_free(q, z), 0, "asked kernel to check freshness");
	CHECK(a = pdu_recv(z), "failed to get reply from kernel");
	is_string(pdu_type(a), "OK", "got [OK] reply from kernel");
	pdu_free(a);

	/* check for TRANSITION / STATE broadcast PDUs */
	CHECK(q = pdu_recv(sub), "no broadcast PDU received from kernel");
	is(s = pdu_type(q), "TRANSITION", "received an [TRANSITION] PDU in response to staleness");
	is(s = pdu_string(a, 1), "test.state.0",  "TRANSITION[0] is state name"); free(s);
	is(s = pdu_string(a, 2), "1418870107",    "TRANSITION[1] is last seen"); free(s);
	is(s = pdu_string(a, 3), "stale",         "TRANSITION[2] is stale/fresh"); free(s);
	is(s = pdu_string(a, 4), "CRITICAL",      "TRANSITION[3] is new status"); free(s);
	is(s = pdu_string(a, 5), "no results!!!", "TRANSITION[4] is summary"); free(s);
	pdu_free(q);
	CHECK(q = pdu_recv(sub), "no broadcast PDU received from kernel");
	is(s = pdu_type(q), "STATE", "received an [STATE] PDU in response to staleness");
	is(s = pdu_string(a, 1), "test.state.0",  "STATE[0] is state name"); free(s);
	is(s = pdu_string(a, 2), "1418870107",    "STATE[1] is last seen"); free(s);
	is(s = pdu_string(a, 3), "stale",         "STATE[2] is stale/fresh"); free(s);
	is(s = pdu_string(a, 4), "CRITICAL",      "STATE[3] is new status"); free(s);
	is(s = pdu_string(a, 5), "no results!!!", "STATE[4] is summary"); free(s);
	pdu_free(q);

	/* check (post-sweep) status */
	q = pdu_make("GET.STATE", 1, "test.state.0");
	is_int(pdu_send_and_free(q, z), 0, "asked kernel for test.state.0");
	CHECK(a = pdu_recv(z), "failed to get reply from kernel");
	is_string(pdu_type(a), "STATE", "got [STATE] reply for test.state.0");
	is_string(s = pdu_string(a, 1), "test.state.0", "reply was for intended target"); free(s);
	is_string(s = pdu_string(a, 2), "1418870107", "last_seen was set from state file"); free(s);
	is_string(s = pdu_string(a, 3), "stale", "test.state.0 is no longer fresh"); free(s);
	is_string(s = pdu_string(a, 5), "no results!!!",
		"stale message is set for summary"); free(s);
	pdu_free(a);

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);

	vzmq_shutdown(sub,   0);
	vzmq_shutdown(z,   500);
	zmq_ctx_destroy(svr.zmq);

	alarm(0);
	done_testing();
}
