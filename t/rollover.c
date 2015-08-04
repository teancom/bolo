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
	DEBUGGING("t/rollover");
	NEED_FS();
	TIMEOUT(5);

	server_t *svr = CONFIGURE(
		"# sample test config\n"
		"broadcast inproc://test.broadcast\n"
		"listener  inproc://test.listener\n"
		"controller inproc://test.controller\n"
		"dumpfiles t/tmp/dump.\%s\n"
		"savefile  t/tmp/save\n"
		"keysfile  t/tmp/keys\n"
		"window @default 2\n"
		"sample  @default sample1\n"
		"counter @default counter1\n");

	CHECK(svr->zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");

	KERNEL(svr->zmq, svr);
	SCHEDULER(svr->zmq, 200);
	void *super  = SUPERVISOR(svr->zmq);
	void *sub    = SUBSCRIBER(svr->zmq);
	void *client = CLIENT(svr->zmq);

	/* ----------------------------- */

	/* first, the samples */
	send_ok(pdu_make("SAMPLE", 3, "1000" /* ts; abitrary */, "sample1", "10"), client);
	send_ok(pdu_make("SAMPLE", 3, "1000" /* ts; abitrary */, "sample1", "10"), client);
	send_ok(pdu_make("SAMPLE", 3, "1001" /* same window  */, "sample1", "10"), client);
	send_ok(pdu_make("SAMPLE", 3, "1002" /* NEW window!  */, "sample1", "11"), client);

	recv_ok(sub, "SAMPLE", 8, "1000", "sample1", "3" /* n */,
	                          "1.000000e+01" /* min */, "1.000000e+01" /* max */,
	                          "3.000000e+01" /* sum */, "1.000000e+01" /* mean */,
	                          "0.000000e+00" /* var */);

	/* then, the counter */
	send_ok(pdu_make("COUNTER", 3, "2000" /* ts; arbitrary */, "counter1", "1"), client); /* 1 */
	send_ok(pdu_make("COUNTER", 3, "2000" /* ts; arbitrary */, "counter1", "2"), client); /* 3 */
	send_ok(pdu_make("COUNTER", 3, "2000" /* ts; arbitrary */, "counter1", "5"), client); /* 8 */
	send_ok(pdu_make("COUNTER", 3, "2001" /* same window   */, "counter1", "1"), client); /* 9 */
	send_ok(pdu_make("COUNTER", 3, "2002" /* NEW window!   */, "counter1", "1"), client); /* 1 */

	recv_ok(sub, "COUNTER", 3, "2000", "counter1", "9");

	/* ----------------------------- */

	pdu_send_and_free(pdu_make("TERMINATE", 0), super);
	zmq_close(super);
	zmq_close(sub);
	zmq_close(client);
	zmq_ctx_destroy(svr->zmq);

	alarm(0);
	done_testing();
}
