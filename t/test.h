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
#define CHECK(x,msg) if (!(x)) BAIL_OUT(msg)

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
	diag("   begin to differ at [%i:%i]", i / 16, i % 16);
	    diag("      got:");
	diag_hex("   ", actual, n);
	diag(" expected:");
	diag_hex("   ", expect, n);

	munmap(actual, len);
	return 1;
}

HELPER
static void write_file(const char *file, const char *contents, size_t n)
{
	int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	CHECK(fd >= 0, "failed to open test savefile for writing");

	if (contents && n == 0)
		n = strlen(contents);

	write(fd, contents, n);
	close(fd);
}
