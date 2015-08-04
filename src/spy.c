#include "bolo.h"

static const char *STATUS[] = { "OK", "WARNING", "CRITICAL", "UNKNOWN" };

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

	if (configure(argv[1], &s) != 0) {
		perror(argv[1]);
		return 2;
	}
	if (binf_read(&s.db, argv[2]) != 0) {
		perror(argv[2]);
		return 2;
	}

	char *k;
	state_t *state;
	for_each_key_value(&s.db.states, k, state) {
		printf("state :: %s\n", state->name);
		printf("  [%u] %s%s - %s\n", state->status, state->stale ? "(stale) " : "",
			STATUS[state->status], state->summary);
		printf("  last seen %i / expires %i\n", state->last_seen, state->expiry);
		printf("  freshness %i\n", state->type->freshness);
		printf("\n");
	}

	counter_t *counter;
	for_each_key_value(&s.db.counters, k, counter) {
		printf("counter :: %s = %lu\n", counter->name, counter->value);
		printf("  window %i\n", counter->window->time);
		printf("  last seen %i\n", counter->last_seen);
		printf("\n");
	}

	rate_t *rate;
	for_each_key_value(&s.db.rates, k, rate) {
		printf("rate :: %s ( %lu : %lu )\n", rate->name, rate->first, rate->last);
		printf("  window %i\n", rate->window->time);
		printf("  first seen %i / last seen %i\n", rate->first_seen, rate->last_seen);
		printf("\n");
	}

	sample_t *sample;
	for_each_key_value(&s.db.samples, k, sample) {
		printf("sample :: %s\n", sample->name);
		printf("  n=%lu min=%e max=%e sum=%e mean=%e var=%e\n",
			sample->n, sample->min, sample->max, sample->sum, sample->mean, sample->var);
		printf("  window %i\n", sample->window->time);
		printf("  last seen %i\n", sample->last_seen);
		printf("\n");
	}

	return 0;
}
