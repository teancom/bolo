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
#include <getopt.h>
#include <signal.h>

static struct {
	char *config_file;
	int   foreground;
	int   dump_config;
} OPTIONS = { 0 };

int main(int argc, char **argv)
{
	OPTIONS.config_file = strdup(DEFAULT_CONFIG_FILE);
	OPTIONS.foreground  = 0;

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "version",          no_argument, NULL, 'V' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "config",     required_argument, NULL, 'c' },
		{ "dump-config",      no_argument, NULL, 'D' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?VFc:D", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			logger(LOG_DEBUG, "handling -h/-?/--help");
			printf("bolo v%s\n", BOLO_VERSION);
			printf("Usage: bolo [-?hVF] [-c filename]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -F, --foreground     don't daemonize, run in the foreground\n");
			printf("  -c filename          set configuration file (default: " DEFAULT_CONFIG_FILE ")\n");
			printf("  -D, --dump-config    dump configuration to standard output\n");
			exit(0);

		case 'V':
			logger(LOG_DEBUG, "handling -V/--version");
			printf("bolo v%s\n"
			       "Copyright (C) 2015 James Hunt\n",
			       BOLO_VERSION);
			exit(0);

		case 'F':
			logger(LOG_DEBUG, "handling -F/--foreground; turning off daemonize behavior");
			OPTIONS.foreground = 1;
			break;

		case 'c':
			logger(LOG_DEBUG, "handling -c/--config; replacing '%s' with '%s'",
				OPTIONS.config_file, optarg);
			free(OPTIONS.config_file);
			OPTIONS.config_file = strdup(optarg);
			break;

		case 'D':
			logger(LOG_DEBUG, "handling -D/--dump-config; setting behavior to dump configuration");
			OPTIONS.foreground = 1;
			OPTIONS.dump_config = 1;
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	int rc;
	server_t *svr = vmalloc(sizeof(server_t));
	svr->config.listener     = strdup(DEFAULT_LISTENER);
	svr->config.controller   = strdup(DEFAULT_CONTROLLER);
	svr->config.broadcast    = strdup(DEFAULT_BROADCAST);
	svr->config.log_level    = strdup(DEFAULT_LOG_LEVEL);
	svr->config.log_facility = strdup(DEFAULT_LOG_FACILITY);
	svr->config.runas_user   = strdup(DEFAULT_RUNAS_USER);
	svr->config.runas_group  = strdup(DEFAULT_RUNAS_GROUP);
	svr->config.pidfile      = strdup(DEFAULT_PIDFILE);
	svr->config.savefile     = strdup(DEFAULT_SAVEFILE);
	svr->config.keysfile     = strdup(DEFAULT_KEYSFILE);
	svr->config.grace_period = DEFAULT_GRACE_PERIOD;

	svr->interval.tick       = 1000;
	svr->interval.freshness  = 2;
	svr->interval.savestate  = 15;
	svr->interval.sweep      = DEFAULT_SWEEP;

	if (OPTIONS.foreground) {
		log_open("bolo", "stderr");
		log_level(LOG_INFO, NULL);
	}

	rc = configure(OPTIONS.config_file, svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to read configuration from %s\nAborting...\n",
			OPTIONS.config_file);
		deconfigure(svr);
		free(svr);
		return 1;
	}

	if (OPTIONS.dump_config) {
		printf("# bolo configuration\n"
		       "# (from %s)\n\n",
		       OPTIONS.config_file);

		printf("listener    %s\n"
		       "controller  %s\n"
		       "broadcast   %s\n\n",
		       svr->config.listener,
		       svr->config.controller,
		       svr->config.broadcast);

		printf("user        %s\n"
		       "group       %s\n"
		       "pidfile     %s\n\n",
		       svr->config.runas_user,
		       svr->config.runas_group,
		       svr->config.pidfile);

		printf("savefile    %s\n"
		       "keysfile    %s\n"
		       "max.events  %u%s\n\n",
		       svr->config.savefile,
		       svr->config.keysfile,
		       (svr->config.events_keep == EVENTS_KEEP_NUMBER
		           ? svr->config.events_max
		           : svr->config.events_max / 60),
		       (svr->config.events_keep == EVENTS_KEEP_NUMBER ? "" : "m"));

		printf("grace.period %u\n"
		       "log %s %s\n\n",
		       svr->config.grace_period,
		       svr->config.log_level,
		       svr->config.log_facility);

		type_t *t;
		char *k;
		for_each_key_value(&svr->db.types, k, t) {
			printf("type %s {\n"
			       "    freshness %u\n"
			       "    %-9s \"%s\"\n"
			       "}\n\n",
			       k, t->freshness,
			       (t->status == WARNING  ? "warning" :
			        t->status == CRITICAL ? "critical" : "unknown"),
			       t->summary);
		}

		int n = 0;
		state_t *state;
		for_each_key_value(&svr->db.states, k, state) {
			printf("state %s %s\n", state->type->name, state->name);
			n++;
		}
		if (n)
			printf("\n");

		n = 0;
		window_t *win;
		for_each_key_value(&svr->db.windows, k, win) {
			printf("window %s %u\n", k, win->time);
			n++;
		}
		if (n)
			printf("\n");

		n = 0;
		counter_t *counter;
		for_each_key_value(&svr->db.counters, k, counter) {
			if (counter->window->name)
				printf("counter %s %s\n", counter->window->name, counter->name);
			else
				printf("counter %u %s\n", counter->window->time, counter->name);
			n++;
		}
		if (n)
			printf("\n");

		n = 0;
		sample_t *sample;
		for_each_key_value(&svr->db.samples, k, sample) {
			if (sample->window->name)
				printf("sample %s %s\n", sample->window->name, sample->name);
			else
				printf("sample %u %s\n", sample->window->time, sample->name);
			n++;
		}
		if (n)
			printf("\n");

		n = 0;
		rate_t *rate;
		for_each_key_value(&svr->db.rates, k, rate) {
			if (rate->window->name)
				printf("rate %s %s\n", rate->window->name, rate->name);
			else
				printf("rate %u %s\n", rate->window->time, rate->name);
			n++;
		}

		deconfigure(svr);
		free(svr);
		return 0;
	}

	void *zmq = zmq_ctx_new();
	if (!zmq) {
		fprintf(stderr, "Failed to initialize 0MQ\n");
		deconfigure(svr);
		free(svr);
		return 2;
	}

	log_level(0, svr->config.log_level);
	if (OPTIONS.foreground) {
		logger(LOG_NOTICE, "starting up (in foreground mode)");

	} else {
		log_open("bolo", svr->config.log_facility);
		logger(LOG_NOTICE, "starting up");

		if (daemonize(svr->config.pidfile, svr->config.runas_user, svr->config.runas_group) != 0) {
			logger(LOG_CRIT, "daemonization failed!");
			deconfigure(svr);
			free(svr);
			return 2;
		}
	}
	logger(LOG_INFO, "log level is %s", svr->config.log_level);

	sigset_t blocked;
	sigfillset(&blocked);
	rc = pthread_sigmask(SIG_BLOCK, &blocked, NULL);
	if (rc != 0) {
		fprintf(stderr, "Failed to block signals in main threads\nAborting...\n");
		deconfigure(svr);
		return 2;
	}

	rc = core_kernel_thread(zmq, svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up database manager thread\nAborting...\n");
		deconfigure(svr);
		free(svr);
		return 2;
	}
	rc = core_scheduler_thread(zmq, 1000);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up scheduler thread\nAborting...\n");
		deconfigure(svr);
		free(svr);
		return 2;
	}

	rc = core_supervisor(zmq);
	logger(LOG_NOTICE, "shutting down");
	return rc;
}
