#include "test.h"

TESTS {
	subtest {
		is_null(bolo_result(NULL, 0), "NULL bolo packet is invalid");
		is_null(bolo_result("x", 1),  "short bolo packet is invalid");
	}

	subtest {
		const char *raw;
		result_t   *res;

		/* v=1, ts=1418758670, len=38, status=1
		   name    'state.test'
		   summary 'this is a test' */
		raw = "\x01\x01\0\0\0\0\0&T\x90\x8a\x0estate.test\0this is a test\0";

		res = bolo_result(raw, 38);
		isnt_null(res, "bolo_result() returns a non-NULL pointer");
		isnt_null(res->_packet, "result has a sub-packet");

		is_unsigned(res->timestamp, 1418758670, "parsed timestamp from bolo packet");
		is_unsigned(res->status,    WARNING,    "parsed status code from bolo packet");

		is_string(res->name,    "state.test",     "parsed state name from bolo packet");
		is_string(res->summary, "this is a test", "parsed output from bolo packet");

		result_free(res);
	}
}
