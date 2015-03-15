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
	void *z;
	pthread_t tid;

	svr.interval.tick      = 10; /* ms */
	svr.interval.freshness = 1;
	svr.interval.savestate = 3;

	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(z = zmq_socket(svr.zmq, ZMQ_ROUTER),
		"failed to create mock kernel test socket");
	CHECK(zmq_bind(z, KERNEL_ENDPOINT) == 0,
		"failed to connect to kernel socket");
	CHECK(pthread_create(&tid, NULL, scheduler, &svr) == 0,
		"failed to spin up scheduler thread");

	/* ----------------------------- */

	size_t checkfresh = 0;
	size_t savestate  = 0;
	size_t tick       = 0;

	alarm(20);
	for (;;) {
		pdu_t *q, *a;

		q = pdu_recv(z);
		isnt_null(q, "received PDU from scheduler");
		if (strcmp(pdu_type(q), "SAVESTATE") == 0) {
			savestate++;
		} else if (strcmp(pdu_type(q), "CHECKFRESH") == 0) {
			checkfresh++;
		} else if (strcmp(pdu_type(q), "TICK") == 0) {
			tick++;
		} else {
			diag("received a [%s] PDU", pdu_type(q));
			BAIL_OUT("Unepxected PDU from scheduler");
		}

		a = pdu_reply(q, "OK", 0);
		pdu_send_and_free(a, z);
		pdu_free(q);

		if (tick == 6)
			break;
	}

	is_int(checkfresh, 6, "received six [CHECKFRESH]");
	is_int(savestate,  2, "received two [SAVESTATE]");
	is_int(tick,       6, "received six [TICK]");

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);

	vzmq_shutdown(z, 0);
	zmq_ctx_destroy(svr.zmq);
}
