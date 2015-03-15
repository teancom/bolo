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
	server_t svr;
	void *z, *kernel;
	pthread_t tid;

	svr.config.controller = "inproc://stat";
	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(kernel = zmq_socket(svr.zmq, ZMQ_ROUTER),
		"failed to create mock kernel test socket");
	CHECK(zmq_bind(kernel, KERNEL_ENDPOINT) == 0,
		"failed to bind to kernel socket");
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
	p = pdu_make("@INVALID!", 2, "foo", "baz");
	is_int(pdu_send_and_free(p, z), 0,
		"sent [@INVALID!] PDU to controller");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "ERROR", "controller replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "Invalid PDU", "Error message returned"); free(s);
	pdu_free(p);

	/* send a state PDU */
	p = pdu_make("STATE", 1, "random.metric");
	is_int(pdu_send_and_free(p, z), 0,
		"sent [STATE] PDU to controller");

		/* be the DB manager for a bit */
		q = pdu_recv(kernel);
		pdu_send_and_free(pdu_reply(q, "STATE", 5, "xyzzy", "1234", "no", "2", "foo"), kernel);
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

	/* send a dump PDU */
	p = pdu_make("DUMP", 0);
	is_int(pdu_send_and_free(p, z), 0,
		"sent [DUMP] PDU to controller");

		/* be the DB manager for a bit */
		q = pdu_recv(kernel);
		write_file("t/tmp/dump", "this is the\ncontent stuff\n", 0);
		pdu_send_and_free(pdu_reply(q, "DUMP", 1, "t/tmp/dump"), kernel);
		/* --------------------------- */

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "DUMP", "controller replied with a [DUMP]");
	is_string(s = pdu_string(p, 1),
		"this is the\ncontent stuff\n",
		"controller returns contents of file"); free(s);
	pdu_free(p);

	/* DUMP failure from kernel */
	p = pdu_make("DUMP", 0);
	is_int(pdu_send_and_free(p, z), 0,
		"sent [DUMP] PDU to controller");

		/* be the DB manager for a bit */
		q = pdu_recv(kernel);
		pdu_send_and_free(pdu_reply(q, "ERROR", 1, "stuff broke"), kernel);
		/* --------------------------- */

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "ERROR", "controller replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "stuff broke",
		"controller relayed error message"); free(s);
	pdu_free(p);


	/* DUMP failure from kernel (enoent) */
	p = pdu_make("DUMP", 0);
	is_int(pdu_send_and_free(p, z), 0,
		"sent [DUMP] PDU to controller");

		/* be the DB manager for a bit */
		q = pdu_recv(kernel);
		unlink("t/tmp/enoent");
		pdu_send_and_free(pdu_reply(q, "DUMP", 1, "t/tmp/enoent"), kernel);
		/* --------------------------- */

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from controller");
	is_string(pdu_type(p), "ERROR", "controller replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "Internal Error",
		"controller generated error message"); free(s);
	pdu_free(p);


	/* FIXME: send a GET.KEYS PDU */
	/* FIXME: send a DEL.KEYS PDU */
	/* FIXME: send a SEARCH.KEYS PDU */


	/* send a GET.EVENTS PDU */
	ok(pdu_send_and_free(pdu_make("GET.EVENTS", 1, "32"), z) == 0,
		"sent [GET.EVENTS] to listener");

		/* go be the kernel */
		q = pdu_recv(kernel);
		isnt_null(q, "received a packet as the kernel");
		is_int(pdu_size(q), 3, "GET.EVENTS packet is 3 frames long");
		is(pdu_type(q), "GET.EVENTS", "packet is a [GET.EVENTS] packet");
		isnt_null(s = pdu_string(p, 1), "GET.EVENTS[0] is not null");
		ok(s && strlen(s) >= 4, "GET.EVENTS[0] is at least 4 characters long");
		free(s);

		is_string(s = pdu_string(p, 2), "32", "GET.EVENTS[1] is timestamp");
		free(s);

		write_file("t/tmp/file.name", "X\n", 2);
		CHECK(pdu_send_and_free(pdu_reply(q, "EVENTS", 1,
			"t/tmp/file.name"), kernel) == 0,
			"failed to send EVENTS reply to our [GET.EVENTS]");
		pdu_free(q);

		/* go be the client */
		p = pdu_recv(z);
		isnt_null(p, "received a reply from the listener");
		is(pdu_type(p), "EVENTS", "listener replied [EVENTS]");
		is(s = pdu_string(p, 1), "X\n", "listener read t/tmp/file.name and returned it");
		free(s);



	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);

	vzmq_shutdown(z,        0);
	vzmq_shutdown(kernel, 500);
	zmq_ctx_destroy(svr.zmq);

	alarm(0);
	done_testing();
}
