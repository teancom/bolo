#include "test.h"

#define TEST_ENDPOINT "inproc://test"

TESTS {
	alarm(5);

	void *zmq;
	void *server;
	pdu_t *p;
	char *s;

	CHECK(zmq = zmq_ctx_new(), "failed to create a new 0MQ context");

	CHECK(server = zmq_socket(zmq, ZMQ_PULL),
		"failed to create mock test socket");
	CHECK(zmq_bind(server, TEST_ENDPOINT) == 0,
		"failed to bind mock test socket to " TEST_ENDPOINT);

	ok(submit_state(zmq, TEST_ENDPOINT,
			"my.example.state", /* name */
			WARNING,            /* status */
			"is broken"         /* message */
		) == 0,
		"submitted a STATE PDU via submit_state()");

	p = pdu_recv(server);
	isnt_null(p, "received PDU on mock test socket");
	is(pdu_type(p), "STATE", "received PDU is a [STATE] PDU");
	is_int(pdu_size(p), 5, "STATE PDU is 5 frames long");

	is(s = pdu_string(p, 1), "my.example.state", "STATE name sent correctly"); free(s);
	/* frame #2 is the timestamp */
	is(s = pdu_string(p, 3), "1",                "STATE status sent correctly"); free(s);
	is(s = pdu_string(p, 4), "is broken",        "STATE message sent correctly"); free(s);
	pdu_free(p);


	ok(submit_counter(zmq, TEST_ENDPOINT,
			"things.that.break", /* name */
			42                   /* increment */
		) == 0,
		"submitted a COUNTER PDU via submit_counter()");

	p = pdu_recv(server);
	isnt_null(p, "received PDU on mock test socket");
	is(pdu_type(p), "COUNTER", "received PDU is a [COUNTER] PDU");
	is_int(pdu_size(p), 4, "COUNTER PDU is 4 frames long");

	is(s = pdu_string(p, 1), "things.that.break", "COUNTER name sent correctly"); free(s);
	/* frame #2 is the timestamp */
	is(s = pdu_string(p, 3), "42",                "COUNTER value sent correctly"); free(s);
	pdu_free(p);


	ok(submit_sample(zmq, TEST_ENDPOINT,
			"cpu.usage.real", /* name */
			2,                /* number of samples taken */
			1.443e2,          /* first sample */
			1.395e2           /* ... */
		) == 0,
		"submitted a SAMPLE PDU via submit_sample()");

	p = pdu_recv(server);
	isnt_null(p, "received PDU on mock test socket");
	is(pdu_type(p), "SAMPLE", "received PDU is a [SAMPLE] PDU");
	is_int(pdu_size(p), 5, "SAMPLE PDU is 5 frames long");

	is(s = pdu_string(p, 1), "cpu.usage.real", "SAMPLE name sent correctly"); free(s);
	/* frame #2 is the timestamp */
	is(s = pdu_string(p, 3), "144.300000", "SAMPLE value #1 sent correctly"); free(s);
	is(s = pdu_string(p, 4), "139.500000", "SAMPLE value #2 sent correctly"); free(s);
	pdu_free(p);


	ok(submit_rate(zmq, TEST_ENDPOINT,
			"packets.rx",   /* name */
			1.294831112e10  /* absolute tick value */
		) == 0,
		"submitted a RATE PDU via submit_rate()");

	p = pdu_recv(server);
	isnt_null(p, "received PDU on mock test socket");
	is(pdu_type(p), "RATE", "received PDU is a [RATE] PDU");
	is_int(pdu_size(p), 4, "RATE PDU is 4 frames long");

	is(s = pdu_string(p, 1), "packets.rx",         "RATE name sent correctly"); free(s);
	/* frame #2 is the timestamp */
	is(s = pdu_string(p, 3), "12948311120.000000", "RATE tick value sent correctly"); free(s);
	pdu_free(p);


	ok(submit_setkeys(zmq, TEST_ENDPOINT,
			3,                           /* # of keys */
			"host:df:/",                 /* key without explicit value */
			"host:sysname=myhostname",   /* key with a value */
			"host:sysdescr=stuff=good"   /* multiple equal signs */
		) == 0,
		"submitted a SETKEYS PDU via submit_setkeys()");

	p = pdu_recv(server);
	isnt_null(p, "received PDU on mock test socket");
	is(pdu_type(p), "SETKEYS", "received PDU is a [SETKEYS] PDU");
	is_int(pdu_size(p), 8, "SETKEYS PDU is 8 frames long");

	/* frame #1 is the timestamp */
	is(s = pdu_string(p, 2), "host:df:/",     "SETKEYS pair #1 key sent correctly"); free(s);
	is(s = pdu_string(p, 3), "1",             "SETKEYS pair #1 (implicit) value sent correctly"); free(s);
	is(s = pdu_string(p, 4), "host:sysname",  "SETKEYS pair #2 key sent correctly"); free(s);
	is(s = pdu_string(p, 5), "myhostname",    "SETKEYS pair #2 value sent correctly"); free(s);
	is(s = pdu_string(p, 6), "host:sysdescr", "SETKEYS pair #3 key sent correctly"); free(s);
	is(s = pdu_string(p, 7), "stuff=good",    "SETKEYS pair #3 value sent correctly"); free(s);
	pdu_free(p);


	ok(submit_event(zmq, TEST_ENDPOINT,
			"a.curious.transpiration",                     /* name */
			"something quite curious has just happened!"   /* extra */
		) == 0,
		"submitted an EVENT PDU via submit_event()");

	p = pdu_recv(server);
	isnt_null(p, "received PDU on mock test socket");
	is(pdu_type(p), "EVENT", "received PDU is a [EVENT] PDU");
	is_int(pdu_size(p), 4, "EVENT PDU is 4 frames long");

	is(s = pdu_string(p, 1), "a.curious.transpiration", "EVENT name sent correctly"); free(s);
	/* frame #2 is the timestamp */
	is(s = pdu_string(p, 3), "something quite curious has just happened!",
		"EVENT extra data sent correctly"); free(s);
	pdu_free(p);

	vzmq_shutdown(server, 0);
	zmq_ctx_destroy(zmq);
}
