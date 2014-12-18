#include "test.h"
#include <sys/mman.h>
#include <zmq.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


/* FIXME: move this into ctap proper */
#define CHECK(x,msg) if (!(x)) BAIL_OUT(msg)

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
static void diag_hex(const char *pre, const char *buf, size_t n)
{
	char *p, line[16 * 3 + 1];
	size_t i, no;

	diag("%s         0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15", pre);
	for (i = no = 0; i < n; no++) {
		p = line;

#define HEXEN \
	*p++ = HEX[(buf[i] & 0xf0) >> 4]; \
	*p++ = HEX[(buf[i] & 0x0f)];      \
	*p++ = ' '; i++

		switch (i % 16) {
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
		*p = '\0';
		diag("%s % 4u | %s", pre, no, line);
	}
	diag("");
}

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
	diag("   begin to differ at [%i:%i]", i / 16, i % 16);
	    diag("      got:");
	diag_hex("   ", actual, n);
	diag(" expected:");
	diag_hex("   ", expect, n);

	munmap(actual, len);
	return 1;
}

static void write_file(const char *file, const char *contents, size_t n)
{
	int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	CHECK(fd >= 0, "failed to open test savefile for writing");

	write(fd, contents, n);
	close(fd);
}

TESTS {
	server_t svr;
	int rc;
	void *z;
	pthread_t tid;

	mkdir("t/tmp", 0755);
	svr.dumpfile_fmt = "t/tmp/dump.%s";
	write_file(svr.savefile = "t/tmp/save",
		"BOLO\0\1\0\0T\x92J\x97\0\0\0\2"
		"\0%T\x92=[\2\0test.state.1\0critically-ness\0"
		"\0%T\x92=[\1\0test.state.0\0its problematic\0"
		"\0\0", 92);

	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(pthread_create(&tid, NULL, db_manager, &svr) == 0,
		"failed to spin up db manager thread");
	CHECK(z = zmq_socket(svr.zmq, ZMQ_DEALER),
		"failed to create mock db manager test socket");
	CHECK(zmq_connect(z, DB_MANAGER_ENDPOINT) == 0,
		"failed to connect to db manager socket");

	/* ----------------------------- */

	pdu_t *p;
	uint32_t time = time_s();
	char *ts = string("%u", time);
	char *s;

	/* send an invalid PDU */
	p = pdu_make("@INVALID!", 2, "foo", "bar");
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent [@INVALID!] PDU to db manager");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from db manager");
	is_string(pdu_type(p), "ERROR", "db manager replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "Invalid PDU", "Error message returned"); free(s);
	pdu_free(p);

	/* get the state of test.state.0 */
	p = pdu_make("STATE", 1, "test.state.0");
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent [STATE] PDU to db manager");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from db manager");
	is_string(pdu_type(p), "STATE", "db manager replied with an [OK]");
	is_string(s = pdu_string(p, 1), "test.state.0",    "STATE[0] is state name"); free(s);
	is_string(s = pdu_string(p, 2), "1418870107",      "STATE[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "yes",             "STATE[2] is freshness boolean"); free(s);
	is_string(s = pdu_string(p, 4), "WARNING",         "STATE[3] is status"); free(s);
	is_string(s = pdu_string(p, 5), "its problematic", "STATE[4] is summary"); free(s);
	pdu_free(p);

	/* send test.state.0 initial ok */
	p = pdu_make("UPDATE", 4, ts, "test.state.0", "0", "all good");
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent [UPDATE] PDU to db manager");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from db manager");
	is_string(pdu_type(p), "OK", "db manager replied with an [OK]");
	pdu_free(p);

	/* send test.state.1 initial crit */
	p = pdu_make("UPDATE", 4, ts, "test.state.1", "2", "critically-ness");
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent 2nd [UPDATE] PDU to db manager");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from db manager");
	is_string(pdu_type(p), "OK", "db manager replied with an [OK]");
	pdu_free(p);

	/* dump the state file (to /t/tmp/dump.test) */
	unlink("t/tmp/dump.test");
	p = pdu_make("DUMP", 1, "test");
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent [DUMP] PDU to db manager");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from db manager");
	is_string(pdu_type(p), "DUMP", "db manager replied with a [DUMP]");
	is_string(s = pdu_string(p, 1), "t/tmp/dump.test", "[DUMP] reply returned file path"); free(s);
	pdu_free(p);

	file_is("t/tmp/dump.test",
		s = string("---\n"
		           "# generated by bolo\n"
		           "test.state.0:\n"
		           "  status:    OK\n"
		           "  message:   all good\n"
		           "  last_seen: %s\n"
		           "  fresh:     yes\n"
		           "test.state.1:\n"
		           "  status:    CRITICAL\n"
		           "  message:   critically-ness\n"
		           "  last_seen: %s\n"
		           "  fresh:     yes\n", ts, ts),
		"dumped YAML file"); free(s);

	/* get state of test.state.1 via [STATE] */
	p = pdu_make("STATE", 1, "test.state.1");
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent [STATE] query to db manager");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from db manager");
	is_string(pdu_type(p), "STATE", "db manager replied with a [STATE]");
	is_string(s = pdu_string(p, 1), "test.state.1",    "STATE[0] is state name"); free(s);
	is_string(s = pdu_string(p, 2), ts,                "STATE[1] is last seen ts"); free(s);
	is_string(s = pdu_string(p, 3), "yes",             "STATE[2] is freshness boolean"); free(s);
	is_string(s = pdu_string(p, 4), "CRITICAL",        "STATE[3] is status"); free(s);
	is_string(s = pdu_string(p, 5), "critically-ness", "STATE[4] is summary"); free(s);
	pdu_free(p);

	/* get non-existent state via [STATE] */
	p = pdu_make("STATE", 1, "fail.enoent//0");
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent [STATE] query to db manager");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from db manager");
	is_string(pdu_type(p), "ERROR", "db manager replied with an [ERROR]");
	is_string(s = pdu_string(p, 1), "State Not Found", "Error message returned"); free(s);
	pdu_free(p);

	/* save state (to /t/tmp/save) */
	p = pdu_make("SAVESTATE", 1, "test");
	rc = pdu_send_and_free(p, z);
	is_int(rc, 0, "sent [SAVESTATE] PDU to db manager");

	p = pdu_recv(z);
	isnt_null(p, "received reply PDU from db manager");
	is_string(pdu_type(p), "OK", "db manager replied with a [SAVESTATE]");
	pdu_free(p);

	s = calloc(83, sizeof(char));
	memcpy(s, "BOLO"     /* H:magic      +4 */
	          "\0\1"     /* H:version    +2 */
	          "\0\0"     /* H:flags      +2 */
	          "...."     /* H:timestamp  +4 (to be filled in later) */
	          "\0\0\0\2" /* H:count      +4 */              /* +16 */

	          "\0\x1e"   /* 0:len        +2 */
	          "...."     /* 0:last_seen  +4 (to be filled in later) */
	          "\0"       /* 0:status     +1 */
	          "\0"       /* 0:stale      +1 */          /* +16 +8 */
	          "test.state.0\0"
	          "all good\0"                           /* +16 +30 */

	          "\0\x25"   /* 1:len        +2 */
	          "...."     /* 1:last_seen  +4 (to be filled in later) */
	          "\2"       /* 1:status     +1 */
	          "\0"       /* 1:stale      +1 */
	          "test.state.1\0"
	          "critically-ness\0"
	          "", 83);
	*(uint32_t*)(s+4+2+2)  = htonl(time);
	*(uint32_t*)(s+16+2)   = htonl(time);
	*(uint32_t*)(s+16+30+2) = htonl(time);

	binfile_is("t/tmp/save", s, 83,
		"save file (binary)");

	/* ----------------------------- */
	pthread_cancel(tid);
	zmq_close(z);
}
