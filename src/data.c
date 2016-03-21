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

void rate_reset(rate_t *r)
{
	r->first_seen = r->last_seen = 0;
	r->first = r->last = 0;
}

int rate_data(rate_t *r, uint64_t v)
{
	r->last = v;
	if (!r->last_seen)
		r->first = v;
	return 0;
}

double rate_calc(rate_t *r, int32_t span)
{
	if (!r->last_seen)
		return 0.0;

	uint64_t diff;
	if (r->last >= r->first) {
		diff = r->last - r->first;
	} else {
		if (r->first < 0xffff) { /* 32-bit rollover */
			diff = 0xffff - r->first + r->last;
		} else { /* 64-bit rollover */
			diff = 0xffffffff - r->first + r->last;
		}
	}
	return diff * 1.0 / (r->last_seen - r->first_seen) * span;
}

state_t *find_state(db_t *db, const char *name)
{
	state_t *x = hash_get(&db->states, name);
	if (x) return x;

	/* check the regex rules */
	re_state_t *re;
	for_each_object(re, &db->state_matches, l) {
		if (pcre_exec(re->re, re->re_extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
			x = calloc(1, sizeof(state_t));
			hash_set(&db->states, name, x);
			x->name    = strdup(name);
			x->type    = re->type;
			x->status  = PENDING;
			x->expiry  = re->type->freshness + time_s();
			x->summary = strdup("(state is pending results)");
			x->ignore  = 0;
			return x;
		}
	}

	return NULL;
}

counter_t *find_counter(db_t *db, const char *name)
{
	counter_t *x = hash_get(&db->counters, name);
	if (x) return x;

	/* check the regex rules */
	re_counter_t *re;
	for_each_object(re, &db->counter_matches, l) {
		if (pcre_exec(re->re, re->re_extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
			x = calloc(1, sizeof(counter_t));
			hash_set(&db->counters, name, x);
			x->name    = strdup(name);
			x->window  = re->window;
			x->value   = 0;
			x->ignore  = 0;
			return x;
		}
	}

	return NULL;
}

sample_t *find_sample(db_t *db, const char *name)
{
	sample_t *x = hash_get(&db->samples, name);
	if (x) return x;

	/* check the regex rules */
	re_counter_t *re;
	for_each_object(re, &db->sample_matches, l) {
		if (pcre_exec(re->re, re->re_extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
			x = calloc(1, sizeof(sample_t));
			hash_set(&db->samples, name, x);
			x->name    = strdup(name);
			x->window  = re->window;
			x->n       = 0;
			x->ignore  = 0;
			return x;
		}
	}

	return NULL;
}

rate_t *find_rate(db_t *db, const char *name)
{
	rate_t *x = hash_get(&db->rates, name);
	if (x) return x;

	/* check the regex rules */
	re_rate_t *re;
	for_each_object(re, &db->rate_matches, l) {
		if (pcre_exec(re->re, re->re_extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
			x = calloc(1, sizeof(rate_t));
			hash_set(&db->rates, name, x);
			x->name   = strdup(name);
			x->window = re->window;
			x->ignore = 0;
			return x;
		}
	}

	return NULL;
}
