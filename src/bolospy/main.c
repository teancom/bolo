/*
  Copyright (c) 2016 The Bolo Authors.  All Rights Reserved.

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

#include "../bolo.h"

typedef struct {
	int   type;
	void *ptr;
} boxed_t;

static const char *STATUS[] = { "OK", "WARNING", "CRITICAL", "UNKNOWN" };

#define BOXED_STATE   1
#define BOXED_SAMPLE  2
#define BOXED_COUNTER 3
#define BOXED_RATE    4

static void box(int type, void *ptr, char *key, strings_t *sort, hash_t *index)
{
	boxed_t *box = vmalloc(sizeof(box));
	box->type = type;
	box->ptr  = ptr;
	strings_add(sort, key);
	hash_set(index, key, box);
	free(key);
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "USAGE: %s bolo.cfg /path/to/save.db\n", argv[0]);
		return 1;
	}

	server_t s;
	memset(&s, 0, sizeof(s));
	s.config.listener     = strdup(DEFAULT_LISTENER);
	s.config.controller   = strdup(DEFAULT_CONTROLLER);
	s.config.broadcast    = strdup(DEFAULT_BROADCAST);
	s.config.log_level    = strdup(DEFAULT_LOG_LEVEL);
	s.config.log_facility = strdup(DEFAULT_LOG_FACILITY);
	s.config.runas_user   = strdup(DEFAULT_RUNAS_USER);
	s.config.runas_group  = strdup(DEFAULT_RUNAS_GROUP);
	s.config.pidfile      = strdup(DEFAULT_PIDFILE);
	s.config.savefile     = strdup(DEFAULT_SAVEFILE);
	s.config.save_size    = DEFAULT_SAVE_SIZE;

	if (configure(argv[1], &s) != 0) {
		perror(argv[1]);
		return 2;
	}
	if (binf_read(&s.db, argv[2], s.config.save_size) != 0) {
		fprintf(stderr, "%s: %s\n", argv[2],
			errno == 0 ? "corrupt savefile" : strerror(errno));
		return 2;
	}

	strings_t *sort = strings_new(NULL);
	hash_t index; memset(&index, 0, sizeof(hash_t));

	char *k;
	state_t *state;
	for_each_key_value(&s.db.states, k, state) {
		box(BOXED_STATE, state, string("%s (state)", state->name), sort, &index);
	}

	counter_t *counter;
	for_each_key_value(&s.db.counters, k, counter) {
		box(BOXED_COUNTER, counter, string("%s (counter)", counter->name), sort ,&index);
	}

	rate_t *rate;
	for_each_key_value(&s.db.rates, k, rate) {
		box(BOXED_RATE, rate, string("%s (rate)", rate->name), sort ,&index);
	}

	sample_t *sample;
	for_each_key_value(&s.db.samples, k, sample) {
		box(BOXED_SAMPLE, sample, string("%s (sample)", sample->name), sort ,&index);
	}

	strings_sort(sort, STRINGS_ASC);
	int i;
	for_each_string(sort, i) {
		boxed_t *box = hash_get(&index, sort->strings[i]);
		switch (box->type) {
		case BOXED_STATE:
			state = (state_t*)(box->ptr);
			printf("state :: %s\n", state->name);
			printf("  [%u] %s%s - %s\n", state->status, state->stale ? "(stale) " : "",
			                             STATUS[state->status], state->summary);
			printf("  last seen %i / expires %i\n", state->last_seen, state->expiry);
			printf("  freshness %i\n", state->type->freshness);
			printf("\n");
			break;

		case BOXED_SAMPLE:
			sample = (sample_t*)(box->ptr);
			printf("sample :: %s\n", sample->name);
			printf("  n=%lu min=%e max=%e sum=%e mean=%e var=%e\n",
			          sample->n, sample->min, sample->max, sample->sum,
			          sample->mean, sample->var);
			printf("  window %i\n", sample->window->time);
			printf("  last seen %i\n", sample->last_seen);
			printf("\n");
			break;

		case BOXED_COUNTER:
			counter = (counter_t*)(box->ptr);
			printf("counter :: %s = %lu\n", counter->name, counter->value);
			printf("  window %i\n", counter->window->time);
			printf("  last seen %i\n", counter->last_seen);
			printf("\n");
			break;

		case BOXED_RATE:
			rate = (rate_t*)(box->ptr);
			printf("rate :: %s ( %lu : %lu )\n", rate->name, rate->first, rate->last);
			printf("  window %i\n", rate->window->time);
			printf("  first seen %i / last seen %i\n", rate->first_seen, rate->last_seen);
			printf("\n");
			break;

		default:
			break;
		}
	}

	return 0;
}
