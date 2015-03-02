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
	int rc;
	void *z;
	pthread_t tid;

	memset(&svr, 0, sizeof(svr));
	list_init(&svr.db.events);
	svr.config.broadcast = "inproc://bcast";
	svr.config.dumpfiles = "t/tmp/dump.%s";
	write_file(svr.config.savefile = "t/tmp/save",
		"FAILURE", 7);
	unlink(svr.config.keysfile = "t/tmp/keys");

	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(pthread_create(&tid, NULL, kernel, &svr) == 0,
		"failed to spin up kernel thread");
	CHECK(z = zmq_socket(svr.zmq, ZMQ_DEALER),
		"failed to create mock kernel test socket");
	CHECK(zmq_connect(z, KERNEL_ENDPOINT) == 0,
		"failed to connect to kernel socket");
	sleep_ms(50);

	/* ----------------------------- */

	pdu_t *p;
	uint32_t time = time_s();

	/* save state (to /t/tmp/save) */
	p = pdu_make("SAVESTATE", 0);
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent [SAVESTATE] PDU to kernel");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from kernel");
	is_string(pdu_type(p), "OK", "kernel replied with a [SAVESTATE]");
	pdu_free(p);

	char s[18];
	memcpy(s, "BOLO\0\1\0\0....\0\0\0\0" "\0\0", 16 + 2);
	*(uint32_t*)(s+8) = htonl(time);
	binfile_is("t/tmp/save", s, 18, "empty save file");

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);
	zmq_close(z);
}
