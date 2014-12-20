#include "test.h"

TESTS {
	server_t svr;
	void *z, *dbman;
	pthread_t tid;

	svr.config.controller = "inproc://stat";
	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(dbman = zmq_socket(svr.zmq, ZMQ_ROUTER),
		"failed to create mock db manager test socket");
	CHECK(zmq_bind(dbman, DB_MANAGER_ENDPOINT) == 0,
		"failed to bind to db manager socket");
	CHECK(pthread_create(&tid, NULL, controller, &svr) == 0,
		"failed to spin up controller thread");

	CHECK(z = zmq_socket(svr.zmq, ZMQ_DEALER),
		"failed to create stat client socket");
	CHECK(zmq_connect(z, svr.config.controller) == 0,
		"failed to create stat client socket");
	sleep_ms(50);

	/* ----------------------------- */

	pdu_t *p, *q;
	char *s;

	/* send an invalid PDU */
	alarm(5);
	p = pdu_make("@INVALID!", 2, "foo", "baz");
	is_int(pdu_send_and_free(p, z), 0,
		"sent [@INVALID!] PDU to controller");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "ERROR", "controller replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "Invalid PDU", "Error message returned"); free(s);
	pdu_free(p);
	alarm(0);

	/* send a state PDU */
	alarm(5);
	p = pdu_make("STATE", 1, "random.metric");
	is_int(pdu_send_and_free(p, z), 0,
		"sent [STATE] PDU to controller");

		/* be the DB manager for a bit */
		q = pdu_recv(dbman);
		pdu_send_and_free(pdu_reply(q, "STATE", 5, "xyzzy", "1234", "no", "2", "foo"), dbman);
		pdu_free(q);
		/* --------------------------- */

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "STATE", "controller replied with a [STATE]");
	is_string(s = pdu_string(p, 1), "xyzzy", "controller relayed result frame 1"); free(s);
	is_string(s = pdu_string(p, 2), "1234",  "controller relayed result frame 2"); free(s);
	is_string(s = pdu_string(p, 3), "no",    "controller relayed result frame 3"); free(s);
	is_string(s = pdu_string(p, 4), "2",     "controller relayed result frame 4"); free(s);
	is_string(s = pdu_string(p, 5), "foo",   "controller relayed result frame 5"); free(s);
	is_null(pdu_string(p, 6), "controller only relays 5 frames (there is no 6th)");
	pdu_free(p);
	alarm(0);

	/* send a dump PDU */
	alarm(5);
	p = pdu_make("DUMP", 0);
	is_int(pdu_send_and_free(p, z), 0,
		"sent [DUMP] PDU to controller");

		/* be the DB manager for a bit */
		q = pdu_recv(dbman);
		write_file("t/tmp/dump", "this is the\ncontent stuff\n", 0);
		pdu_send_and_free(pdu_reply(q, "DUMP", 1, "t/tmp/dump"), dbman);
		/* --------------------------- */

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "DUMP", "controller replied with a [DUMP]");
	is_string(s = pdu_string(p, 1),
		"this is the\ncontent stuff\n",
		"controller returns contents of file"); free(s);
	pdu_free(p);
	alarm(0);

	/* DUMP failure from dbman */
	alarm(5);
	p = pdu_make("DUMP", 0);
	is_int(pdu_send_and_free(p, z), 0,
		"sent [DUMP] PDU to controller");

		/* be the DB manager for a bit */
		q = pdu_recv(dbman);
		pdu_send_and_free(pdu_reply(q, "ERROR", 1, "stuff broke"), dbman);
		/* --------------------------- */

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "ERROR", "controller replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "stuff broke",
		"controller relayed error message"); free(s);
	pdu_free(p);
	alarm(0);


	/* DUMP failure from dbman (enoent) */
	alarm(5);
	p = pdu_make("DUMP", 0);
	is_int(pdu_send_and_free(p, z), 0,
		"sent [DUMP] PDU to controller");

		/* be the DB manager for a bit */
		q = pdu_recv(dbman);
		unlink("t/tmp/enoent");
		pdu_send_and_free(pdu_reply(q, "DUMP", 1, "t/tmp/enoent"), dbman);
		/* --------------------------- */

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "ERROR", "controller replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "Internal Error",
		"controller generated error message"); free(s);
	pdu_free(p);
	alarm(0);


	/* ----------------------------- */
	pthread_cancel(tid);
	zmq_close(z);
}
