#include "test.h"

TESTS {
	server_t s;
	void *dbman, *z;
	pthread_t tid;

	s.config.listener = "inproc://listener";

	CHECK(s.zmq = zmq_ctx_new(), "failed to create a new 0MQ context");
	CHECK(dbman = zmq_socket(s.zmq, ZMQ_ROUTER),
		"failed to create mock db manager test socket");
	CHECK(zmq_bind(dbman, DB_MANAGER_ENDPOINT) == 0,
		"failed to bind mock db manager test socket to endpoint");

	CHECK(pthread_create(&tid, NULL, listener, &s) == 0,
		"failed to spin up listener thread");
	sleep_ms(50);

	CHECK(z = zmq_socket(s.zmq, ZMQ_DEALER),
		"failed to create test DEALER socket");
	CHECK(zmq_connect(z, s.config.listener) == 0,
		"failed to connect test socket to listener");

	/* ----------------------------- */

	pdu_t *q, *a;

	ok(pdu_send_and_free(pdu_make("SUBMIT", 3,
			"host.example.com:cpu_usage", /* state name */
			"0",                          /* status code */
			"looks good, 42.5\% usage"),  /* summary message */
		z) == 0, "sent [SUBMIT] to listener");

		/* go be the db manager */
		q = pdu_recv(dbman);
		isnt_null(q, "received a packet as the db manager");
		is(pdu_type(q), "UPDATE", "packet is an [UPDATE] packet");
		ok(abs(atoi(pdu_string(q, 1)) - time_s()) < 5,     "UPDATE[0] is timestamp");
		is(pdu_string(q, 2), "host.example.com:cpu_usage", "UPDATE[1] is name");
		is(pdu_string(q, 3), "0",                          "UPDATE[2] is status");
		is(pdu_string(q, 4), "looks good, 42.5\% usage",   "UPDATE[3] is summary");

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), dbman) == 0,
			"failed to send OK reply to our UPDATE");
		pdu_free(q);

	/* back to the client */
	a = pdu_recv(z);
	isnt_null(a, "recevied a reply from the listener");
	is_string(pdu_type(a), "OK", "listener replied [OK]");

	/* try a bad PDU */
	ok(pdu_send_and_free(pdu_make("ZORK", 0), z) == 0, "sent [ZORK] to listener");
	a = pdu_recv(z);
	isnt_null(a, "recevied a reply from the listener");
	is_string(pdu_type(a), "ERROR", "listener replied [ERROR]");
	is_string(pdu_string(a, 1), "Invalid PDU", "Error message from listener");

	/* ----------------------------- */
	pthread_cancel(tid);
}
