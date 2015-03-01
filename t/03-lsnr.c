#include "test.h"

TESTS {
	mkdir("t/tmp", 0755);

	alarm(5);
	server_t s;
	void *kernel, *z;
	pthread_t tid;
	char *x;

	s.config.listener = "inproc://listener";

	CHECK(s.zmq = zmq_ctx_new(), "failed to create a new 0MQ context");
	CHECK(kernel = zmq_socket(s.zmq, ZMQ_ROUTER),
		"failed to create mock kernel test socket");
	CHECK(zmq_bind(kernel, KERNEL_ENDPOINT) == 0,
		"failed to bind mock kernel test socket to endpoint");

	CHECK(pthread_create(&tid, NULL, listener, &s) == 0,
		"failed to spin up listener thread");
	sleep_ms(50);

	CHECK(z = zmq_socket(s.zmq, ZMQ_DEALER),
		"failed to create test DEALER socket");
	CHECK(zmq_connect(z, s.config.listener) == 0,
		"failed to connect test socket to listener");

	/* ----------------------------- */

	pdu_t *q, *a;

	/* send a STATE update */
	ok(pdu_send_and_free(pdu_make("STATE", 4,
			"12345",                      /* timestamp */
			"host.example.com:cpu_usage", /* state name */
			"0",                          /* status code */
			"looks good, 42.5\% usage"),  /* summary message */
		z) == 0, "sent [STATE] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is(pdu_type(q), "PUT.STATE", "packet is a [PUT.STATE] packet");
		is(pdu_string(q, 1), "12345",                      "PUT.STATE[0] is timestamp");
		is(pdu_string(q, 2), "host.example.com:cpu_usage", "PUT.STATE[1] is name");
		is(pdu_string(q, 3), "0",                          "PUT.STATE[2] is status");
		is(pdu_string(q, 4), "looks good, 42.5\% usage",   "PUT.STATE[3] is summary");

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.STATE]");
		pdu_free(q);

		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %x >>", x); free(x);

	/* send a COUNTER update */
	ok(pdu_send_and_free(pdu_make("COUNTER", 3,
			"12345",                      /* timestamp */
			"host.example.com:logins",    /* counter name */
			"3"),                         /* increment value */
		z) == 0, "sent [COUNTER] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is(pdu_type(q), "PUT.COUNTER", "packet is a [PUT.COUNTER] packet");
		is(pdu_string(q, 1), "12345",                   "PUT.COUNTER[0] is timestamp");
		is(pdu_string(q, 2), "host.example.com:logins", "PUT.COUNTER[1] is name");
		is(pdu_string(q, 3), "3",                       "PUT.COUNTER[2] is increment");

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.COUNTER]");
		pdu_free(q);

		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);

	/* send a COUNTER update without explicit increment */
	ok(pdu_send_and_free(pdu_make("COUNTER", 2,
			"12345",                      /* timestamp */
			"host.example.com:logins"),   /* counter name */
		z) == 0, "sent [COUNTER] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is(pdu_type(q), "PUT.COUNTER", "packet is a [PUT.COUNTER] packet");
		is(pdu_string(q, 1), "12345",                   "PUT.COUNTER[0] is timestamp");
		is(pdu_string(q, 2), "host.example.com:logins", "PUT.COUNTER[1] is name");
		is(pdu_string(q, 3), "1",                       "implicit increment");

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.COUNTER]");
		pdu_free(q);

		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);

	/* send a SAMPLE update */
	ok(pdu_send_and_free(pdu_make("SAMPLE", 3,
			"12345",                /* timestamp */
			"host.example.com:cpu"  /* sample name */,
			"42.7"),                /* sample value */
		z) == 0, "sent [SAMPLE] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is(pdu_type(q), "PUT.SAMPLE", "packet is a [PUT.SAMPLE] packet");
		is(pdu_string(q, 1), "12345",                  "PUT.SAMPLE[0] is timestamp");
		is(pdu_string(q, 2), "host.example.com:cpu",   "PUT.SAMPLE[1] is name");
		is(pdu_string(q, 3), "42.7",                   "PUT.SAMPLE[2] is value");

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.SAMPLE]");
		pdu_free(q);

		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);

	/* send an EVENT */
	ok(pdu_send_and_free(pdu_make("EVENT", 3,
			"11223344",
			"my.event.name",
			"{special:'data'}"),
		z) == 0, "sent an [EVENT] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is(pdu_type(q), "NEW.EVENT", "packet is a [NEW.EVENT] packet");
		is(pdu_string(q, 1), "11223344",         "NEW.EVENT[0] is timestamp");
		is(pdu_string(q, 2), "my.event.name",    "NEW.EVENT[1] is name");
		is(pdu_string(q, 3), "{special:'data'}", "NEW.EVENT[2] is extra data");

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [NEW.EVENT]");
		pdu_free(q);

		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);

	/* try a bad PDU */
	ok(pdu_send_and_free(pdu_make("ZORK", 0), z) == 0, "sent [ZORK] to listener");
	a = pdu_recv(z);
	isnt_null(a, "received a reply from the listener");
	is_string(pdu_type(a), "ERROR", "listener replied [ERROR]");
	is_string(pdu_string(a, 1), "Invalid PDU", "Error message from listener");

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);
}
