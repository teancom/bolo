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

#include <ctap.h>
#include "../src/bolo.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <zmq.h>

#define HELPER __attribute__((unused))

/* FIXME: move this into ctap proper */
#define CHECK(x,msg) do { if (!(x)) { diag(strerror(errno)); BAIL_OUT(msg); } } while (0)

/*************************************************/

HELPER
static int send_ok(pdu_t *pdu, void *zocket)
{
	is_int(pdu_send(pdu, zocket), 0, "sent [%s] PDU", pdu_type(pdu));
	pdu_free(pdu);
	return 0;
}

HELPER
static int recv_ok(void *zocket, const char *type, int n, ...)
{
	va_list ap;

	pdu_t *pdu = pdu_recv(zocket);
	isnt_null(pdu, "received a PDU");
	if (!pdu)
		return 0;

	is(pdu_type(pdu), type, "received a [%s] PDU", pdu_type(pdu));
	if (strcmp(pdu_type(pdu), type) != 0) {
		pdu_free(pdu);
		return 0;
	};

	is_int(pdu_size(pdu), n + 1, "PDU received is %i data frames long", n);
	if (pdu_size(pdu) != n + 1) {
		pdu_free(pdu);
		return 0;
	}

	va_start(ap, n);
	int i;
	for (i = 0; i < n; i++) {
		const char *expect = va_arg(ap, const char *);
		char *s = pdu_string(pdu, i+1);
		is_string(s, expect, "[%s] data frame #%i", type, i+1);
		free(s);
	}
	va_end(ap);
	pdu_free(pdu);

	return 0;
}

HELPER
static int file_is(const char *path, const char *expect, const char *msg)
{
	FILE *io = fopen(path, "r");
	CHECK(io, "failed to open file for file_is() check");

	fseek(io, 0, SEEK_END);
	size_t len = ftell(io);
	rewind(io);

	char *actual = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fileno(io), 0);
	CHECK(actual, "failed to mmap target file for file_is() check");

	is_int(len, strlen(expect), "%s (file length)", msg);
	is_string(actual, expect, "%s", msg);

	return 0;
}

static const char *HEX = "0123456789abcdef";
HELPER
static void diag_hex(const char *pre, const char *buf, size_t n)
{
	char *p, line[16 * 3 + 1];
	char *a, ascii[16 + 1];
	size_t i, no;

	diag("%s         0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15", pre);
	diag("%s       --- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --", pre);
	for (i = no = 0; i < n; no++) {
		p = line;
		a = ascii;

#define HEXEN \
	*p++ = HEX[(buf[i] & 0xf0) >> 4]; \
	*p++ = HEX[(buf[i] & 0x0f)];      \
	*a++ = (isprint(buf[i]) ? buf[i] : '.'); \
	*p++ = ' '; i++

		switch ((n - i) > 16 ? 0 : n % 16) {
			case  0: HEXEN;
			case 15: HEXEN;
			case 14: HEXEN;
			case 13: HEXEN;
			case 12: HEXEN;
			case 11: HEXEN;
			case 10: HEXEN;
			case  9: HEXEN;
			case  8: HEXEN;
			case  7: HEXEN;
			case  6: HEXEN;
			case  5: HEXEN;
			case  4: HEXEN;
			case  3: HEXEN;
			case  2: HEXEN;
			case  1: HEXEN;
		}
#undef HEXEN
		*p = *a = '\0';
		diag("%s % 4u | %- 48s        %s", pre, no, line, ascii);
	}
	diag("");
}

HELPER
static int binfile_is(const char *path, const char *expect, size_t n, const char *msg)
{
	FILE *io = fopen(path, "r");
	CHECK(io, "failed to open file for binfile_is() check");

	fseek(io, 0, SEEK_END);
	size_t len = ftell(io);
	rewind(io);

	char *actual = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fileno(io), 0);
	CHECK(actual, "failed to mmap target file for binfile_is() check");

	is_int(len, n, "%s (file length)", msg);
	if (memcmp(actual, expect, n) == 0) {
		ok(1, "%s", msg);
		munmap(actual, len);
		return 0;
	}

	size_t i;
	for (i = 0; i < n; i++)
		if (actual[i] != expect[i])
			break;

	ok(0, "%s", msg);
	diag("   begin to differ at %i [%i:%i]", i, i / 16, i % 16);
	    diag("      got: %02x", actual[i] & 0xff);
	diag_hex("   ", actual, n);
	diag(" expected: %02x", expect[i] & 0xff);
	diag_hex("   ", expect, n);

	munmap(actual, len);
	return 1;
}

HELPER
static void write_file(const char *file, const char *contents, ssize_t n)
{
	int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	CHECK(fd >= 0, "failed to open test savefile for writing");

	if (contents && n == 0)
		n = strlen(contents);

	CHECK(write(fd, contents, n) == n, "write_file() - short write detected");
	close(fd);
}

HELPER
static int within(double a, double b, double ep)
{
	double diff = a - b;
	int rc = (diff > 0 ? diff < ep : diff > -1 * ep);
	if (!rc) {
		diag("");
		diag("expected %e to be within +/-%0.4f", a, ep);
		diag("      of %e (was %0.4f off)", b, diff);
		diag("");
	}
	return rc;
}

/*************************************************/

HELPER
void TIME_ALIGN(void)
{
	struct timeval tv;
	CHECK(gettimeofday(&tv, NULL) == 0, "failed to gettimeofday()");
	sleep_ms(1000 - tv.tv_usec / 1000);
	CHECK(gettimeofday(&tv, NULL) == 0, "failed to gettimeofday()");
	diag("tv.usec = %lu", tv.tv_usec);
}

HELPER
void DEBUGGING(const char *prog)
{
	if (getenv("DEBUG_TESTS")) {
		log_open(prog, "console");
		log_level(0, "debug");
	}
}

HELPER
void NEED_FS(void)
{
	system("/bin/rm -rf t/tmp/");
	mkdir("t/tmp", 0755);
}

HELPER
void TIMEOUT(int s)
{
	alarm(s);
}

HELPER
void KERNEL(void *zmq, server_t *server)
{
	CHECK(core_kernel_thread(zmq, server) == 0,
		"failed to spin up kernel thread");
}

HELPER
void SCHEDULER(void *zmq, int ms)
{
	CHECK(core_scheduler_thread(zmq, ms) == 0,
		"failed to spin up scheduler thread");
}

HELPER
void* SUPERVISOR(void *zmq)
{
	void* super;

	CHECK(super = zmq_socket(zmq, ZMQ_PUB),
		"failed to create supervisor control socket");
	CHECK(zmq_bind(super, "inproc://bolo/v1/supervisor.command") == 0,
		"failed to bind supervisor control socket");

	return super;
}

HELPER
void* SUBSCRIBER(void *zmq)
{
	void *sub;

	CHECK(sub = zmq_socket(zmq, ZMQ_SUB),
		"failed to create kernel subscriber socket");
	CHECK(zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0) == 0,
		"failed to set subscriber filter");
	CHECK(zmq_connect(sub, "inproc://test.broadcast") == 0,
		"failed to connect to broadcast endpoint");

	return sub;
}

HELPER
void* MANAGER(void *zmq)
{
	void *mgr;

	CHECK(mgr = zmq_socket(zmq, ZMQ_DEALER),
		"failed to create kernel manager socket");
	CHECK(zmq_connect(mgr, "inproc://test.controller") == 0,
		"failed to connect to manager endpoint");

	return mgr;
}

HELPER
void* CLIENT(void *zmq)
{
	void *client;

	CHECK(client = zmq_socket(zmq, ZMQ_PUSH),
		"failed to create mock kernel test socket");
	CHECK(zmq_connect(client, "inproc://test.listener") == 0,
		"failed to connect to kernel socket");

	return client;
}

HELPER
server_t* CONFIGURE(const char *config)
{
	server_t *svr = vmalloc(sizeof(server_t));
	svr->interval.tick      = 1000;
	svr->interval.freshness = 1;
	svr->interval.savestate = 15;

	char *s = string(""
		"controller inproc://test.controller\n"
		"listener   inproc://test.listener\n"
		"broadcast  inproc://test.broadcast\n"
		"savefile   t/tmp/save\n"
		"keysfile   t/tmp/keys\n"
		"%s", config);
	write_file("t/tmp/config", s, 0); free(s);
	CHECK(configure("t/tmp/config", svr) == 0,
		"failed to configure bolo");
	return svr;
}
