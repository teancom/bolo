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

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <vigor.h>
#include <bolo.h>

#define ME "bolo2meta"

typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */

	void *subscriber; /* SUB:  subscriber to bolo broadcast port (external) */

	reactor_t *reactor;
} dispatcher_t;

int dispatcher_thread(void *zmq, const char *endpoint);

static int _dispatcher_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	dispatcher_t *dispatcher = (dispatcher_t*)_;

	if (socket == dispatcher->control)
		return VIGOR_REACTOR_HALT;

	if (socket == dispatcher->subscriber) {
		const char *metric;

		if (strcmp(pdu_type(pdu), "COUNTER") == 0) {
			if (pdu_size(pdu) == 4) {
				metric = "counter";
			} else {
				metric = "bogon.counter";
			}

		} else if (strcmp(pdu_type(pdu), "SAMPLE") == 0) {
			if (pdu_size(pdu) == 9) {
				metric = "sample";
			} else {
				metric = "bogon.sample";
			}

		} else if (strcmp(pdu_type(pdu), "RATE") == 0) {
			if (pdu_size(pdu) == 5) {
				metric = "rate";
			} else {
				metric = "bogon.rate";
			}

		} else if (strcmp(pdu_type(pdu), "STATE") == 0) {
			if (pdu_size(pdu) == 6) {
				metric = "state";
			} else {
				metric = "bogon.state";
			}

		} else if (strcmp(pdu_type(pdu), "TRANSITION") == 0) {
			if (pdu_size(pdu) == 6) {
				metric = "transition";
			} else {
				metric = "bogon.transition";
			}

		} else if (strcmp(pdu_type(pdu), "EVENT") == 0) {
			if (pdu_size(pdu) == 4) {
				metric = "event";
			} else {
				metric = "bogon.event";
			}

		} else if (strcmp(pdu_type(pdu), "SET.KEYS") == 0) {
			if (pdu_size(pdu) % 2 == 0) {
				metric = "setkeys";
			} else {
				metric = "bogon.setkeys";
			}

		} else {
			metric = "bogon.unknown";
		}

		pdu_send_and_free(pdu_make("COUNT", 1, metric), dispatcher->monitor);
		pdu_send_and_free(pdu_make("COUNT", 1, "total"), dispatcher->monitor);
		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
static void * _dispatcher_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	dispatcher_t *dispatcher = (dispatcher_t*)_;

	reactor_go(dispatcher->reactor);

	logger(LOG_DEBUG, "dispatcher: shutting down");

	zmq_close(dispatcher->subscriber);
	zmq_close(dispatcher->monitor);
	zmq_close(dispatcher->control);

	reactor_free(dispatcher->reactor);
	free(dispatcher);

	logger(LOG_DEBUG, "dispatcher: terminated");
	return NULL;
}
/* }}} */
int dispatcher_thread(void *zmq, const char *endpoint) /* {{{ */
{
	assert(zmq != NULL);
	assert(endpoint != NULL);

	int rc;

	logger(LOG_INFO, "initializing dispatcher thread");

	dispatcher_t *dispatcher = vmalloc(sizeof(dispatcher_t));


	logger(LOG_DEBUG, "connecting dispatcher.control -> supervisor");
	rc = bolo_subscriber_connect_supervisor(zmq, &dispatcher->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting dispatcher.monitor -> monitor");
	rc = bolo_subscriber_connect_monitor(zmq, &dispatcher->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting dispatcher.subscriber <- bolo at %s", endpoint);
	dispatcher->subscriber = zmq_socket(zmq, ZMQ_SUB);
	if (!dispatcher->subscriber)
		return -1;
	rc = zmq_setsockopt(dispatcher->subscriber, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;
	rc = vzmq_connect_af(dispatcher->subscriber, endpoint, AF_UNSPEC);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "setting up dispatcher event reactor");
	dispatcher->reactor = reactor_new();
	if (!dispatcher->reactor)
		return -1;

	logger(LOG_DEBUG, "dispatcher: registering dispatcher.control with event reactor");
	rc = reactor_set(dispatcher->reactor, dispatcher->control, _dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: registering dispatcher.subscriber with event reactor");
	rc = reactor_set(dispatcher->reactor, dispatcher->subscriber, _dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _dispatcher_thread, dispatcher);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

int main(int argc, char **argv) /* {{{ */
{
	struct {
		char *endpoint;

		int   verbose;
		int   daemonize;

		char *pidfile;
		char *user;
		char *group;

		char *submit_to;
		char *prefix;
	} OPTIONS = {
		.verbose   = 0,
		.endpoint  = strdup("tcp://127.0.0.1:2997"),
		.submit_to = strdup("tcp://127.0.0.1:2999"),
		.daemonize = 1,
		.pidfile   = strdup("/var/run/" ME ".pid"),
		.user      = strdup("root"),
		.group     = strdup("root"),
		.prefix    = NULL, /* will be set later, if needed */
	};

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "version",          no_argument, NULL, 'V' },
		{ "verbose",          no_argument, NULL, 'v' },
		{ "endpoint",   required_argument, NULL, 'e' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "pidfile",    required_argument, NULL, 'p' },
		{ "user",       required_argument, NULL, 'u' },
		{ "group",      required_argument, NULL, 'g' },
		{ "submit",     required_argument, NULL, 'S' },
		{ "prefix",     required_argument, NULL, 'P' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:Fp:u:g:S:P:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf(ME " v%s\n", BOLO_VERSION);
			printf("Usage: " ME " [-h?FVv] [-e tcp://host:port] [-P PREFIX] [-S tcp://host:port]\n"
			       "                 [-u user] [-g group] [-p /path/to/pidfile]\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -F, --foreground     don't daemonize, stay in the foreground\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -P, --prefix         prefix for submitting perf metrics back\n");
			printf("  -S, --submit         bolo listener endpoint for perf metrics\n");
			printf("  -u, --user           user to run as (if daemonized)\n");
			printf("  -g, --group          group to run as (if daemonized)\n");
			printf("  -p, --pidfile        where to store the pidfile (if daemonized)\n");
			exit(0);

		case 'V':
			printf(ME " v%s\n"
			       "Copyright (c) 2016 The Bolo Authors.  All Rights Reserved.\n",
			       BOLO_VERSION);
			exit(0);

		case 'v':
			OPTIONS.verbose++;
			break;

		case 'e':
			free(OPTIONS.endpoint);
			OPTIONS.endpoint = strdup(optarg);
			break;

		case 'F':
			OPTIONS.daemonize = 0;
			break;

		case 'p':
			free(OPTIONS.pidfile);
			OPTIONS.pidfile = strdup(optarg);
			break;

		case 'u':
			free(OPTIONS.user);
			OPTIONS.user = strdup(optarg);
			break;

		case 'g':
			free(OPTIONS.group);
			OPTIONS.group = strdup(optarg);
			break;

		case 'S':
			free(OPTIONS.submit_to);
			OPTIONS.submit_to = strdup(optarg);
			break;

		case 'P':
			free(OPTIONS.prefix);
			OPTIONS.prefix = strdup(optarg);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	if (!OPTIONS.prefix) {
		char *s = fqdn();
		OPTIONS.prefix = string("%s:sys:bolo", s);
		free(s);
	}

	if (OPTIONS.daemonize) {
		log_open(ME, "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			exit(3);
		}
		umask(um);
	} else {
		log_open(ME, "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");


	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		exit(1);
	}

	int rc;
	rc = bolo_subscriber_init();
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize subscriber architecture");
		exit(2);
	}

	rc = bolo_subscriber_monitor_thread(zmq, OPTIONS.prefix, OPTIONS.submit_to);
	if (rc != 0) {
		logger(LOG_ERR, "failed to spin up monitor thread");
		exit(2);
	}

	rc = bolo_subscriber_scheduler_thread(zmq, 5 * 1000);
	if (rc != 0) {
		logger(LOG_ERR, "failed to spin up scheduler thread");
		exit(2);
	}

	rc = bolo_subscriber_metrics(zmq,
		"COUNT",  "counter",    "COUNT",  "bogon.counter",
		"COUNT",  "sample",     "COUNT",  "bogon.sample",
		"COUNT",  "rate",       "COUNT",  "bogon.rate",
		"COUNT",  "state",      "COUNT",  "bogon.state",
		"COUNT",  "transition", "COUNT",  "bogon.transition",
		"COUNT",  "event",      "COUNT",  "bogon.event",
		"COUNT",  "bogon.unknown", NULL);
	if (rc != 0) {
		logger(LOG_WARNING, "failed to provision metrics");
		/* non-fatal, but still a problem... */
	}

	rc = dispatcher_thread(zmq, OPTIONS.endpoint);
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize dispatcher: %s", strerror(errno));
		exit(2);
	}

	bolo_subscriber_supervisor(zmq);

	free(OPTIONS.endpoint);

	free(OPTIONS.pidfile);
	free(OPTIONS.user);
	free(OPTIONS.group);

	free(OPTIONS.submit_to);
	free(OPTIONS.prefix);

	return 0;
}
/* }}} */
