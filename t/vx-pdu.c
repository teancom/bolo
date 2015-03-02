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
	subtest { /* basic dup */
		char *s;
		pdu_t *orig = pdu_make("ORIGINAL", 4, "this", "is", "A", "test");
		pdu_t *copy = vx_pdu_dup(orig, NULL);

		isnt_null(copy, "pdu_dup() created a copy PDU");
		is(pdu_type(copy), "ORIGINAL", "PDU type is copied over");
		is_int(pdu_size(copy), 5, "copy PDU is 5 frames long");
		is(s = pdu_string(copy, 1), "this", "COPY[1] == 'this'"); free(s);
		is(s = pdu_string(copy, 2), "is",   "COPY[2] == 'is'"); free(s);
		is(s = pdu_string(copy, 3), "A",    "COPY[3] == 'A'"); free(s);
		is(s = pdu_string(copy, 4), "test", "COPY[4] == 'test'"); free(s);

		pdu_free(copy);
		pdu_free(orig);
	}

	subtest { /* dup with new TYPE */
		char *s;
		pdu_t *orig = pdu_make("ORIGINAL", 4, "this", "is", "A", "test");
		pdu_t *copy = vx_pdu_dup(orig, "A-COPY");

		isnt_null(copy, "pdu_dup() created a copy PDU");
		is(pdu_type(copy), "A-COPY", "PDU type is overridden");
		is_int(pdu_size(copy), 5, "copy PDU is 5 frames long");
		is(s = pdu_string(copy, 1), "this", "COPY[1] == 'this'"); free(s);
		is(s = pdu_string(copy, 2), "is",   "COPY[2] == 'is'"); free(s);
		is(s = pdu_string(copy, 3), "A",    "COPY[3] == 'A'"); free(s);
		is(s = pdu_string(copy, 4), "test", "COPY[4] == 'test'"); free(s);

		pdu_free(copy);
		pdu_free(orig);
	}

	subtest { /* copy */
		char *s;
		pdu_t *orig = pdu_make("ORIGINAL", 4, "this", "is", "A", "test");
		pdu_t *copy = pdu_make("COPY", 1, "it was");
		ok(vx_pdu_copy(copy, orig, 3, 0) == 0, "copied frames to new PDU");

		isnt_null(copy, "pdu_dup() created a copy PDU");
		is(pdu_type(copy), "COPY", "PDU type is set");
		is_int(pdu_size(copy), 4, "copy PDU is 4 frames long");
		is(s = pdu_string(copy, 1), "it was", "COPY[1] == 'it was'"); free(s);
		is(s = pdu_string(copy, 2), "A",      "COPY[2] == 'A'"); free(s);
		is(s = pdu_string(copy, 3), "test",   "COPY[3] == 'test'"); free(s);

		pdu_free(copy);
		pdu_free(orig);
	}

	done_testing();
}
