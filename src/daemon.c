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
#include <signal.h>
#include <getopt.h>

static struct {
	char *config_file;
	int   foreground;
} OPTIONS = { 0 };

/* signal-handling thread; when it exits, main will
   pthread_cancel all of the other threads and then
   join them, in order. */
void* watcher(void *u)
{
	sigset_t *sigs = (sigset_t*)u;
	int s, sig;
	for (;;) {
		s = sigwait(sigs, &sig);
		if (s != 0) {
			errno = s;
			perror("watcher sigwait");
			exit(1);
		}

		logger(LOG_ERR, "watcher received signal %i; terminating", sig);
		return NULL;
	}
}

int main(int argc, char **argv)
{
	OPTIONS.config_file = strdup(DEFAULT_CONFIG_FILE);
	OPTIONS.foreground  = 0;

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "version",          no_argument, NULL, 'V' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "config",     required_argument, NULL, 'c' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?VFc:", long_opts, &idx);
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

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	int rc;
	server_t svr;
	pthread_t tid_watcher, tid_db, tid_sched, tid_ctrl, tid_lsnr, tid_nsca;

	memset(&svr, 0, sizeof(svr));
	svr.config.listener     = strdup(DEFAULT_LISTENER);
	svr.config.controller   = strdup(DEFAULT_CONTROLLER);
	svr.config.broadcast    = strdup(DEFAULT_BROADCAST);
	svr.config.log_level    = strdup(DEFAULT_LOG_LEVEL);
	svr.config.log_facility = strdup(DEFAULT_LOG_FACILITY);
	svr.config.runas_user   = strdup(DEFAULT_RUNAS_USER);
	svr.config.runas_group  = strdup(DEFAULT_RUNAS_GROUP);
	svr.config.pidfile      = strdup(DEFAULT_PIDFILE);
	svr.config.savefile     = strdup(DEFAULT_SAVEFILE);
	svr.config.dumpfiles    = strdup(DEFAULT_DUMPFILES);

	svr.interval.tick       = 1000;
	svr.interval.freshness  = 2;
	svr.interval.savestate  = 15;

	if (OPTIONS.foreground) {
		log_open("bolo", "stderr");
		log_level(LOG_INFO, NULL);
	}

	rc = configure(OPTIONS.config_file, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to read configuration from %s\nAborting...\n",
			OPTIONS.config_file);
		deconfigure(&svr);
		return 1;
	}

	svr.zmq = zmq_ctx_new();
	if (!svr.zmq) {
		fprintf(stderr, "Failed to initialize 0MQ\n");
		deconfigure(&svr);
		return 2;
	}

	log_level(0, svr.config.log_level);
	if (OPTIONS.foreground) {
		logger(LOG_NOTICE, "starting up (in foreground mode)");

	} else {
		log_open("bolo", svr.config.log_facility);
		logger(LOG_NOTICE, "starting up");

		if (daemonize(svr.config.pidfile, svr.config.runas_user, svr.config.runas_group) != 0) {
			logger(LOG_CRIT, "daemonization failed!");
			deconfigure(&svr);
			return 2;
		}
	}
	logger(LOG_INFO, "log level is %s", svr.config.log_level);

	sigset_t sigs;
	sigemptyset(&sigs);
	sigaddset(&sigs, SIGQUIT);
	sigaddset(&sigs, SIGINT);
	sigaddset(&sigs, SIGTERM);
	sigaddset(&sigs, SIGUSR1);
	rc = pthread_sigmask(SIG_BLOCK, &sigs, NULL);
	if (rc != 0) {
		fprintf(stderr, "Failed to block signals: %s\nAborting...\n", strerror(errno));
		deconfigure(&svr);
		return 2;
	}
	rc = pthread_create(&tid_watcher, NULL, watcher, &sigs);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up watcher thread\nAborting...\n");
		deconfigure(&svr);
		return 2;
	}

	rc = pthread_create(&tid_db, NULL, kernel, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up database manager thread\nAborting...\n");
		deconfigure(&svr);
		return 2;
	}
	rc = pthread_create(&tid_sched, NULL, scheduler, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up scheduler thread\nAborting...\n");
		deconfigure(&svr);
		return 2;
	}
	rc = pthread_create(&tid_ctrl, NULL, controller, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up controller thread\nAborting...\n");
		deconfigure(&svr);
		return 2;
	}
	rc = pthread_create(&tid_lsnr, NULL, listener, &svr);
	if (rc != 0) {
		fprintf(stderr, "Failed to start up listener thread\nAborting...\n");
		deconfigure(&svr);
		return 2;
	}
	if (svr.config.nsca_port > 0) {
		rc = pthread_create(&tid_nsca, NULL, nsca_gateway, &svr);
		if (rc != 0) {
			fprintf(stderr, "Failed to start up NSCA gateway thread\nAborting...\n");
			deconfigure(&svr);
			return 2;
		}
	}

	void *_;
	pthread_join(tid_watcher, &_);

	pthread_cancel(tid_db);    pthread_join(tid_db,  &_);
	pthread_cancel(tid_sched); pthread_join(tid_sched, &_);
	pthread_cancel(tid_lsnr);  pthread_join(tid_lsnr,  &_);
	pthread_cancel(tid_ctrl);  pthread_join(tid_ctrl,  &_);

	if (svr.config.nsca_port > 0) {
		pthread_cancel(tid_nsca);
		pthread_join(tid_nsca,  &_);
	}

	zmq_ctx_destroy(svr.zmq);
	deconfigure(&svr);

	logger(LOG_NOTICE, "shutting down");
	return 0;
}
