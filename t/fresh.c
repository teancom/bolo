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
	DEBUGGING("t/core");
	NEED_FS();
	TIMEOUT(5);

	server_t *svr = CONFIGURE(
		"type :local { freshness 1 critical \"no results!!!\"\n}\n"
		"use :local\n"
		"state test.state.0\n"
		"state test.state.1\n");

	write_file(svr->config.savefile,
		"BOLO\0\1\0\0T\x92J\x97\0\0\0\2"
		"\0'\0\1T\x92=[\2\0test.state.1\0critically-ness\0"
		"\0'\0\1T\x92=[\1\0test.state.0\0its problematic\0"
		"\0\0", 96);
	/* there is no keysfile */
	unlink(svr->config.keysfile);

	void *zmq;
	CHECK(zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");

	KERNEL(zmq, svr);
	SCHEDULER(zmq, 200);
	void *super = SUPERVISOR(zmq);
	void *sub   = SUBSCRIBER(zmq);
	void *mgr   = MANAGER(zmq);

	/* ----------------------------- */

	/* check (pre-sweep) status */
	send_ok(pdu_make("STATE", 1, "test.state.0"), mgr);
	recv_ok(mgr, "STATE", 5, "test.state.0", "1418870107", /* from state file */
	                         "fresh", "WARNING", "its problematic");

	/* wait enough for freshness to pass */
	sleep_ms(210); /* tick + 10ms should be enough */

	/* check for TRANSITION / STATE broadcast PDUs */
	recv_ok(sub, "TRANSITION", 5, "test.state.0", "1418870107", /* from state_file */
	                              "stale", "CRITICAL", "no results!!!");
	recv_ok(sub, "STATE",      5, "test.state.0", "1418870107", /* from state_file */
	                              "stale", "CRITICAL", "no results!!!");

	/* check (post-sweep) status */
	send_ok(pdu_make("STATE", 1, "test.state.0"), mgr);
	recv_ok(mgr, "STATE", 5, "test.state.0", "1418870107", /* from state_file */
	                         "stale", "CRITICAL", "no results!!!");

	/* ----------------------------- */

	pdu_send_and_free(pdu_make("TERMINATE", 0), super);
	zmq_close(super);
	zmq_close(sub);
	zmq_close(mgr);
	zmq_ctx_destroy(zmq);

	alarm(0);
	done_testing();
}
