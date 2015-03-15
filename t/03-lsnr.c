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

	CHECK(z = zmq_socket(s.zmq, ZMQ_PUSH),
		"failed to create test PUSH socket");
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
		is(x = pdu_string(q, 1), "12345",                      "PUT.STATE[0] is timestamp"); free(x);
		is(x = pdu_string(q, 2), "host.example.com:cpu_usage", "PUT.STATE[1] is name"); free(x);
		is(x = pdu_string(q, 3), "0",                          "PUT.STATE[2] is status"); free(x);
		is(x = pdu_string(q, 4), "looks good, 42.5\% usage",   "PUT.STATE[3] is summary"); free(x);

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.STATE]");
		pdu_free(q);

		/* go be the client */
#if 0
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %x >>", x); free(x);
		pdu_free(a);
#endif

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
		is(x = pdu_string(q, 1), "12345",                   "PUT.COUNTER[0] is timestamp"); free(x);
		is(x = pdu_string(q, 2), "host.example.com:logins", "PUT.COUNTER[1] is name"); free(x);
		is(x = pdu_string(q, 3), "3",                       "PUT.COUNTER[2] is increment"); free(x);

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.COUNTER]");
		pdu_free(q);

#if 0
		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);
		pdu_free(a);
#endif

	/* send a COUNTER update without explicit increment */
	ok(pdu_send_and_free(pdu_make("COUNTER", 2,
			"12345",                      /* timestamp */
			"host.example.com:logins"),   /* counter name */
		z) == 0, "sent [COUNTER] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is(pdu_type(q), "PUT.COUNTER", "packet is a [PUT.COUNTER] packet");
		is(x = pdu_string(q, 1), "12345",                   "PUT.COUNTER[0] is timestamp"); free(x);
		is(x = pdu_string(q, 2), "host.example.com:logins", "PUT.COUNTER[1] is name"); free(x);
		is(x = pdu_string(q, 3), "1",                       "implicit increment"); free(x);

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.COUNTER]");
		pdu_free(q);

#if 0
		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);
		pdu_free(a);
#endif

	/* send a SAMPLE update */
	ok(pdu_send_and_free(pdu_make("SAMPLE", 3,
			"12345",                /* timestamp */
			"host.example.com:cpu", /* sample name */
			"42.7"),                /* sample value */
		z) == 0, "sent [SAMPLE] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is(pdu_type(q), "PUT.SAMPLE", "packet is a [PUT.SAMPLE] packet");
		is(x = pdu_string(q, 1), "12345",                  "PUT.SAMPLE[0] is timestamp"); free(x);
		is(x = pdu_string(q, 2), "host.example.com:cpu",   "PUT.SAMPLE[1] is name"); free(x);
		is(x = pdu_string(q, 3), "42.7",                   "PUT.SAMPLE[2] is value"); free(x);

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.SAMPLE]");
		pdu_free(q);

#if 0
		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);
		pdu_free(a);
#endif

	/* send a RATE value */
	ok(pdu_send_and_free(pdu_make("RATE", 3,
			"12345",          /* timestamp */
			"my.rate.name",   /* rate name */
			"123444"),        /* rate value */
		z) == 0, "sent [RATE] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is(pdu_type(q), "PUT.RATE", "packet is a [PUT.RATE] packet");
		is(x = pdu_string(q, 1), "12345",        "PUT.RATE[0] is timestamp"); free(x);
		is(x = pdu_string(q, 2), "my.rate.name", "PUT.RATE[1] is name"); free(x);
		is(x = pdu_string(q, 3), "123444",       "PUT.RATE[2] is value"); free(x);

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [PUT.RATE]");
		pdu_free(q);

#if 0
		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);
		pdu_free(a);
#endif

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
		is(x = pdu_string(q, 1), "11223344",         "NEW.EVENT[0] is timestamp"); free(x);
		is(x = pdu_string(q, 2), "my.event.name",    "NEW.EVENT[1] is name"); free(x);
		is(x = pdu_string(q, 3), "{special:'data'}", "NEW.EVENT[2] is extra data"); free(x);

		CHECK(pdu_send_and_free(pdu_reply(q, "OK", 0), kernel) == 0,
			"failed to send OK reply to our [NEW.EVENT]");
		pdu_free(q);

#if 0
		/* go be the client */
		a = pdu_recv(z);
		isnt_null(a, "received a reply from the listener");
		is_string(pdu_type(a), "OK", "listener replied [OK]");
		x = pdu_string(a, 1); if (x) diag("<< %s >>", x); free(x);
		pdu_free(a);
#endif

#if 0
	/* try a bad PDU */
	ok(pdu_send_and_free(pdu_make("ZORK", 0), z) == 0, "sent [ZORK] to listener");
	a = pdu_recv(z);
	isnt_null(a, "received a reply from the listener");
	is_string(pdu_type(a), "ERROR", "listener replied [ERROR]");
	is_string(x = pdu_string(a, 1), "Invalid PDU", "Error message from listener"); free(x);
	pdu_free(a);
#endif

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);

	vzmq_shutdown(z,        0);
	vzmq_shutdown(kernel, 500);
	zmq_ctx_destroy(s.zmq);

	alarm(0);
	done_testing();
}
