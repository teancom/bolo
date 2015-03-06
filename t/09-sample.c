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

int within(double a, double b, double ep)
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

TESTS {
	sample_t s;
	memset(&s, 0, sizeof(s));

	ok(s.n == 0, "initial sample 'n' is 0");
	ok(s.min  == 0.0, "initial sample 'min' is 0.00");
	ok(s.max  == 0.0, "initial sample 'max' is 0.00");
	ok(s.sum  == 0.0, "initial sample 'sum' is 0.00");
	ok(s.mean == 0.0, "initial sample 'min' is 0.00");
	ok(s.var  == 0.0, "initial sample 'var' is 0.00");

	ok(sample_data(&s, 2.0) == 0, "add 2.0 data point");
	ok(s.n == 1, "n of [2.0] is 1");
	ok(within(s.min,  2.0, 0.001), "min of [2.0] is 2.0");
	ok(within(s.max , 2.0, 0.001), "max of [2.0] is 2.0");
	ok(within(s.sum,  2.0, 0.001), "sum of [2.0] is 2.0");
	ok(within(s.mean, 2.0, 0.001), "mean of [2.0] is 2.0");
	ok(within(s.var,  0.0, 0.001), "variance of [2.0] is 0 (no variance)");

	ok(sample_data(&s, 3.0) == 0, "add 3.0 data point");
	ok(s.n == 2, "n of [2.0, 3.0] is 2");
	ok(within(s.min,  2.00, 0.001), "min of [2.0, 3.0] is 2.0");
	ok(within(s.max,  3.00, 0.001), "max of [2.0, 3.0] is 3.0");
	ok(within(s.sum,  5.00, 0.001), "max of [2.0, 3.0] is 5.0");
	ok(within(s.mean, 2.50, 0.001), "mean of [2.0, 3.0] is 2.5");
	ok(within(s.var,  0.25, 0.001), "variance of [2.0, 3.0] is 0.25");

	ok(sample_data(&s, 4.1) == 0, "add 4.1 data point");
	ok(sample_data(&s, 6.7) == 0, "add 6.7 data point");
	ok(sample_data(&s, 0.1) == 0, "add 0.1 data point");
	ok(s.n == 5, "n of [2.0, 3.0, 4.1, 6.7, 0.1] is 5");
	ok(within(s.min,  0.10, 0.001), "min of [2.0, 3.0, 4.1, 6.7, 0.1] is 0.1");
	ok(within(s.max,  6.70, 0.001), "max of [2.0, 3.0, 4.1, 6.7, 0.1] is 6.7");
	ok(within(s.sum, 15.90, 0.001), "sum of [2.0, 3.0, 4.1, 6.7, 0.1] is 15.7");
	ok(within(s.mean, 3.18, 0.001), "mean of [2.0, 3.0, 4.1, 6.7, 0.1] is 3.18");
	ok(within(s.var,  4.83, 0.001), "variance of [2.0, 3.0, 4.1, 6.7, 0.1] is 0.25");

	sample_reset(&s);
	ok(s.n == 0, "after reset, 'n' is 0");
	ok(s.min  == 0.0, "after reset, 'min' is 0.00");
	ok(s.max  == 0.0, "after reset, 'max' is 0.00");
	ok(s.sum  == 0.0, "after reset, 'sum' is 0.00");
	ok(s.mean == 0.0, "after reset, 'min' is 0.00");
	ok(s.var  == 0.0, "after reset, 'var' is 0.00");

	memset(&s, 0, sizeof(s));
	size_t i;
	for (i = 0; i < 14; i++)
		sample_data(&s, 1.1);
	sample_data(&s, 109.876);
	ok(s.n == 15, "n of [14x1.1, 109.876] is 15");
	ok(within(s.min,    1.100, 0.001), "min of [14x1.1, 109.876] is 1.1");
	ok(within(s.max,  109.876, 0.001), "max of [14x1.1, 109.876] is 109.876");
	ok(within(s.sum,  125.276, 0.001), "sum of [14x1.1, 109.876] is 125.276");
	ok(within(s.mean,   8.352, 0.001), "mean of [14x1.1, 109.876] is 8.352");
	ok(within(s.var,  736.227, 0.001), "variance of [14x1.1, 109.876] is 736.227");
}
