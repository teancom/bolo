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
	DEBUGGING("t/fail01-db");
	NEED_FS();
	TIMEOUT(5);

	server_t *svr = CONFIGURE("");
	write_file(svr->config.savefile, "FAILURE", 7);

	CHECK(svr->zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");

	KERNEL(svr->zmq, svr);
	void *super = SUPERVISOR(svr->zmq);
	void *mgr   = MANAGER(svr->zmq);

	/* ----------------------------- */

	uint32_t time = time_s();

	/* save state (to /t/tmp/save) */
	send_ok(pdu_make("SAVESTATE", 0), mgr);
	recv_ok(mgr, "OK", 0);

	char s[18];
	memcpy(s, "BOLO\0\1\0\0....\0\0\0\0" "\0\0", 18);
	*(uint32_t*)(s+8) = htonl(time);
	binfile_is("t/tmp/save", s, 18, "empty save file");

	/* ----------------------------- */

	pdu_send_and_free(pdu_make("TERMINATE", 0), super);
	zmq_close(super);
	zmq_close(mgr);
	zmq_ctx_destroy(svr->zmq);
}
