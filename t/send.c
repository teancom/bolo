#include "test.h"

#define TEST_ENDPOINT "inproc://test"

TESTS {
	alarm(5);

	void *zmq;
	void *server;

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
	recv_ok(server, "STATE", 4, NULL, "my.example.state", "1", "is broken");


	ok(submit_counter(zmq, TEST_ENDPOINT,
			"things.that.break", /* name */
			42                   /* increment */
		) == 0,
		"submitted a COUNTER PDU via submit_counter()");
	recv_ok(server, "COUNTER", 3, NULL, "things.that.break", "42");


	ok(submit_sample(zmq, TEST_ENDPOINT,
			"cpu.usage.real", /* name */
			2,                /* number of samples taken */
			1.443e2,          /* first sample */
			1.395e2           /* ... */
		) == 0,
		"submitted a SAMPLE PDU via submit_sample()");
	recv_ok(server, "SAMPLE", 4, NULL, "cpu.usage.real", "144.300000", "139.500000");


	ok(submit_rate(zmq, TEST_ENDPOINT,
			"packets.rx",   /* name */
			12948310        /* absolute tick value */
		) == 0,
		"submitted a RATE PDU via submit_rate()");
	recv_ok(server, "RATE", 3, NULL, "packets.rx", "12948310");


	ok(submit_setkeys(zmq, TEST_ENDPOINT,
			3,                           /* # of keys */
			"host:df:/",                 /* key without explicit value */
			"host:sysname=myhostname",   /* key with a value */
			"host:sysdescr=stuff=good"   /* multiple equal signs */
		) == 0,
		"submitted a SET.KEYS PDU via submit_setkeys()");
	recv_ok(server, "SET.KEYS", 6, "host:df:/",     "1",
	                               "host:sysname",  "myhostname",
	                               "host:sysdescr", "stuff=good");


	ok(submit_event(zmq, TEST_ENDPOINT,
			"a.curious.transpiration",                     /* name */
			"something quite curious has just happened!"   /* extra */
		) == 0,
		"submitted an EVENT PDU via submit_event()");
	recv_ok(server, "EVENT", 3, NULL, "a.curious.transpiration",
	                            "something quite curious has just happened!");

	zmq_close(server);
	zmq_ctx_destroy(zmq);
}
