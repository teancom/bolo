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
#include <getopt.h>

#define FORMAT_PLAIN 0
#define FORMAT_YAML  1

static struct {
	char *config_file;
	char *savedb;
	int   format;
} OPTIONS = { 0 };

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

int cmd_spy(int off, int argc, char **argv)
{
	OPTIONS.config_file = strdup(DEFAULT_CONFIG_FILE);

	struct option long_opts[] = {
		{ "help",         no_argument, NULL, 'h' },
		{ "config", required_argument, NULL, 'c' },
		{ "yaml",         no_argument, NULL, 'Y' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int c, idx = 1;

		c = getopt_long(argc - off, argv + off, "h?c:Y", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf("bolo-spy v%s\n", BOLO_VERSION);
			printf("USAGE: bolo-spy [-h?] [-c /path/to/confi] /path/to/savedb\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -c, --config         path to the bolo configuration\n");
			printf("  -Y, --yaml           dump the savedb as a YAML file\n");
			return 0;

		case 'c':
			free(OPTIONS.config_file);
			OPTIONS.config_file = strdup(optarg);
			break;

		case 'Y':
			OPTIONS.format = FORMAT_YAML;
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	off += optind;
	if (argc - off != 1) {
		fprintf(stderr, "USAGE: bolo spy [OPTIONS] /path/to/save.db\n");
		return 1;
	}
	OPTIONS.savedb = argv[off];

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

	if (configure(OPTIONS.config_file, &s) != 0) {
		perror(OPTIONS.config_file);
		return 2;
	}
	if (binf_read(&s.db, OPTIONS.savedb, s.config.save_size) != 0) {
		fprintf(stderr, "%s: %s\n", OPTIONS.savedb,
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
			if (OPTIONS.format == FORMAT_YAML) {
				printf("%s:\n", state->name);
				printf("  type:      state\n");
				printf("  status:    %s\n", STATUS[state->status]);
				printf("  code:      %u\n", state->status);
				printf("  stale:     %s\n", state->stale ? "yes" : "no");
				printf("  freshness: %i\n", state->type->freshness);
				printf("  last_seen: %i\n", state->last_seen);
				printf("  expiry:    %i\n", state->expiry);
				printf("  summary: |-\n    %s\n", state->summary);
				printf("\n");
			} else {
				printf("state :: %s\n", state->name);
				printf("  [%u] %s%s - %s\n", state->status, state->stale ? "(stale) " : "",
											 STATUS[state->status], state->summary);
				printf("  last seen %i / expires %i\n", state->last_seen, state->expiry);
				printf("  freshness %i\n", state->type->freshness);
				printf("\n");
			}
			break;

		case BOXED_SAMPLE:
			sample = (sample_t*)(box->ptr);
			if (OPTIONS.format == FORMAT_YAML) {
				printf("%s:\n", sample->name);
				printf("  type:      sample\n");
				printf("  n:         %lu\n", sample->n);
				printf("  min:       %e\n", sample->min);
				printf("  max:       %e\n", sample->min);
				printf("  mean:      %e\n", sample->mean);
				printf("  sum:       %e\n", sample->sum);
				printf("  var:       %e\n", sample->var);
				printf("  window:    %i\n", sample->window->time);
				printf("  last_seen: %i\n", sample->last_seen);
				printf("\n");
			} else {
				printf("sample :: %s\n", sample->name);
				printf("  n=%lu min=%e max=%e sum=%e mean=%e var=%e\n",
						  sample->n, sample->min, sample->max, sample->sum,
						  sample->mean, sample->var);
				printf("  window %i\n", sample->window->time);
				printf("  last seen %i\n", sample->last_seen);
				printf("\n");
			}
			break;

		case BOXED_COUNTER:
			counter = (counter_t*)(box->ptr);
			if (OPTIONS.format == FORMAT_YAML) {
				printf("%s:\n", counter->name);
				printf("  type:      counter\n");
				printf("  value:     %lu\n", counter->value);
				printf("  window:    %i\n", counter->window->time);
				printf("  last_seen: %i\n", counter->last_seen);
				printf("\n");
			} else {
				printf("counter :: %s = %lu\n", counter->name, counter->value);
				printf("  window %i\n", counter->window->time);
				printf("  last seen %i\n", counter->last_seen);
				printf("\n");
			}
			break;

		case BOXED_RATE:
			rate = (rate_t*)(box->ptr);
			if (OPTIONS.format == FORMAT_YAML) {
				printf("%s:\n", rate->name);
				printf("  type:        rate\n");
				printf("  first_value: %lu\n", rate->first);
				printf("  last_value:  %lu\n", rate->last);
				printf("  window:      %i\n",  rate->window->time);
				printf("  first_seen:  %i\n",  rate->first_seen);
				printf("  last_seen:   %i\n",  rate->last_seen);
				printf("\n");
			} else {
				printf("rate :: %s ( %lu : %lu )\n", rate->name, rate->first, rate->last);
				printf("  window %i\n", rate->window->time);
				printf("  first seen %i / last seen %i\n", rate->first_seen, rate->last_seen);
				printf("\n");
			}
			break;

		default:
			break;
		}
	}

	return 0;
}
