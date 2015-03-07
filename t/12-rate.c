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

#define T0 1000
#define T1 T0 + 10

TESTS {
	rate_t r = {0};

	is_int(r.first_seen, 0, "initial rate 'first_seen' is 0");
	is_int(r.last_seen,  0, "initial rate 'last_seen' is 0");
	is_int(r.first,      0, "initial rate 'first' value is 0");
	is_int(r.last,       0, "initial rate 'last' value is 0");

	ok(within(rate_calc(&r, 1), 0, 0.001), "empty rate calc yields 0");

	ok(rate_data(&r, 100) == 0, "add rate data point 100");
	r.first_seen = T0; r.last_seen  = T1;
	is_int(r.first,     100, "rate 'first' value is 100");
	is_int(r.last,      100, "rate 'last' value is 100");
	ok(within(rate_calc(&r,  1), 0, 0.001), "calculated rate (per/sec) is 0");
	ok(within(rate_calc(&r, 60), 0, 0.001), "calculated rate (per/min) is 0");
	ok(within(rate_calc(&r,  0), 0, 0.001), "calculated rate (div/0) is 0");

	ok(rate_data(&r, 244) == 0, "add rate data point 244");
	is_int(r.first,     100, "rate 'first' value is 100");
	is_int(r.last,      244, "rate 'last' value is 244");

	ok(within(rate_calc(&r,  1), 14.4, 0.001), "calculated rate (per/sec) is 14.4");
	ok(within(rate_calc(&r, 60), 864.0, 0.001), "calculated rate (per/min) is 864.0");
	ok(within(rate_calc(&r,  0), 0, 0.001), "calculated rate (div/0) is 0");

	rate_reset(&r);
	is_int(r.first_seen, 0, "reset rate 'first_seen' is 0");
	is_int(r.last_seen,  0, "reset rate 'last_seen' is 0");
	is_int(r.first,      0, "reset rate 'first' is 0");
	is_int(r.last,       0, "reset rate 'last' is 0");

	/* 32-bit rollover detection */
	rate_reset(&r);
	ok(rate_data(&r, 0xffff - 20) == 0, "add rate data point max(uint32_t) - 20");
	r.first_seen = T0; r.last_seen = T0 + 1;

	ok(rate_data(&r, 0xffff -  1) == 0, "add rate data point max(uint32_t) -  1");
	ok(within(rate_calc(&r, 1), 19.0, 0.001), "rate calculation at values just under max(uint32_t)");

	ok(rate_data(&r, 7) == 0, "add rate data point 7 (rollover, +8)");
	ok(within(rate_calc(&r, 1), 27.0, 0.001), "rate calculatation accounts for 32-bit rollover");

	/* 64-bit rollover detection */
	rate_reset(&r);
	ok(rate_data(&r, 0xffffffff - 20) == 0, "add rate data point max(uint64_t) - 20");
	r.first_seen = T0; r.last_seen = T0 + 1;

	ok(rate_data(&r, 0xffffffff -  1) == 0, "add rate data point max(uint64_t) -  1");
	ok(within(rate_calc(&r, 1), 19.0, 0.001), "rate calculation at values just under max(uint64_t)");

	ok(rate_data(&r, 7) == 0, "add rate data point 7 (rollover, +8)");
	ok(within(rate_calc(&r, 1), 27.0, 0.001), "rate calculatation accounts for 64-bit rollover");
}
