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

#include "bolo.h"

void sample_reset(sample_t *sample)
{
	sample->last_seen = 0;

	sample->min  = sample->max   = 0;
	sample->sum  = sample->n     = 0;
	sample->mean = sample->mean_ = 0;
	sample->var  = sample->var_  = 0;
}

int sample_data(sample_t *s, double v)
{
	if (s->n == 0) {
		s->min = s->max = v;
	} else {
		if (v < s->min) s->min = v;
		if (v > s->max) s->max = v;
	}
	s->sum += v;
	s->n++;

	s->mean_ = s->mean;
	s->mean  = s->mean_ + (v - s->mean_) / s->n;

	s->var_ = s->var;
	s->var = ( (s->n - 1) * s->var_ + ( (v - s->mean_) * (v - s->mean) ) ) / s->n;

	return 0;
}

void counter_reset(counter_t *counter)
{
	counter->last_seen = 0;
	counter->value = 0;
}
