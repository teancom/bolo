#include "test.h"

TESTS {
	server_t svr; memset(&svr, 0, sizeof(svr));

	mkdir("t/tmp", 0755);
	write_file("t/tmp/config",
		"# sample test config\n"
		"dumpfiles t/tmp/dump.\%s\n"
		"savefile  t/tmp/save\n"
		"type :local { freshness 1 critical \"no results!!!\"\n}\n"
		"use :local\n"
		"state test.state.0\n"
		"state test.state.1\n"
		"", 0);
	CHECK(configure("t/tmp/config", &svr) == 0,
		"failed to configure bolo");

	write_file(svr.config.savefile,
		"BOLO\0\1\0\0T\x92J\x97\0\0\0\2"
		"\0%T\x92=[\2\0test.state.1\0critically-ness\0"
		"\0%T\x92=[\1\0test.state.0\0its problematic\0"
		"\0\0", 92);

	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");

	pthread_t tid;
	CHECK(pthread_create(&tid, NULL, db_manager, &svr) == 0,
		"failed to spin up db manager thread");
	sleep_ms(50);

	void *z;
	CHECK(z = zmq_socket(svr.zmq, ZMQ_DEALER),
		"failed to create mock db manager test socket");
	CHECK(zmq_connect(z, DB_MANAGER_ENDPOINT) == 0,
		"failed to connect to db manager socket");

	/* ----------------------------- */

	char *s;
	pdu_t *q, *a;

	/* check (pre-sweep) status */
	q = pdu_make("STATE", 1, "test.state.0");
	is_int(pdu_send_and_free(q, z), 0, "asked db manager for test.state.0");
	CHECK(a = pdu_recv(z), "failed to get reply from db manager");
	is_string(pdu_type(a), "STATE", "got [STATE] reply for test.state.0");
	is_string(s = pdu_string(a, 1), "test.state.0", "reply was for intended target"); free(s);
	is_string(s = pdu_string(a, 2), "1418870107", "last_seen was set from state file"); free(s);
	is_string(s = pdu_string(a, 3), "yes", "test.state.0 is still fresh"); free(s);
	is_string(s = pdu_string(a, 5), "its problematic", "summary messsage"); free(s);
	pdu_free(a);

	/* wait enough for freshness to pass */
	sleep_ms(1050);

	/* initiate freshness sweep */
	q = pdu_make("CHECKFRESH", 0);
	is_int(pdu_send(q, z), 0, "asked db manager to check freshness");
	CHECK(a = pdu_recv(z), "failed to get reply from db manager");
	is_string(pdu_type(a), "OK", "got [OK] reply from db manager");
	pdu_free(a);

	/* check (post-sweep) status */
	q = pdu_make("STATE", 1, "test.state.0");
	is_int(pdu_send_and_free(q, z), 0, "asked db manager for test.state.0");
	CHECK(a = pdu_recv(z), "failed to get reply from db manager");
	is_string(pdu_type(a), "STATE", "got [STATE] reply for test.state.0");
	is_string(s = pdu_string(a, 1), "test.state.0", "reply was for intended target"); free(s);
	is_string(s = pdu_string(a, 2), "1418870107", "last_seen was set from state file"); free(s);
	is_string(s = pdu_string(a, 3), "no", "test.state.0 is no longer fresh"); free(s);
	is_string(s = pdu_string(a, 5), "no results!!!",
		"stale message is set for summary"); free(s);
	pdu_free(a);

	/* ----------------------------- */
	pthread_cancel(tid);
	zmq_close(z);
}
