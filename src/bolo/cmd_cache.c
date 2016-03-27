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

#include <pthread.h>
#include <signal.h>
#include <getopt.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <vigor.h>
#include <bolo.h>

/**************************/

typedef struct {
	hash_t pdus;      /* map of PDUs, indexed by name, that we've seen */

	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */

	void *subscriber; /* SUB:  subscriber to bolo broadcast port (external) */
	void *broadcast;  /* PUB:  bolo re-broadcast port (external) */
	void *listen;     /* PULL: local listener for prompted re-broadcast requests (external) */

	reactor_t *reactor;
} dispatcher_t;

static int _dispatcher_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	dispatcher_t *dispatcher = (dispatcher_t*)_;

	if (socket == dispatcher->control)
		return VIGOR_REACTOR_HALT;

	if (socket == dispatcher->subscriber) {
		char *key = NULL;

		/* re-broadcast immediately! */
		logger(LOG_DEBUG, "relaying [%s] PDU", pdu_type(pdu));
		pdu_send_and_free(pdu_dup(pdu, NULL), dispatcher->broadcast);

		if (strcmp(pdu_type(pdu), "COUNTER") == 0
		 || strcmp(pdu_type(pdu), "SAMPLE") == 0
		 || strcmp(pdu_type(pdu), "RATE") == 0) {
			key = pdu_string(pdu, 2);

		} else if (strcmp(pdu_type(pdu), "STATE") == 0) {
			key = pdu_string(pdu, 1);
		}

		if (key) {
			logger(LOG_DEBUG, "storing %s PDU for %s for re-broadcast later", pdu_type(pdu), key);

			pdu_t *dupe = pdu_dup(pdu, NULL);
			pdu_t *prev = hash_set(&dispatcher->pdus, key, dupe);
			if (prev != dupe)
				pdu_free(prev);

			free(key);
		}

		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == dispatcher->listen) {
		if (strcmp(pdu_type(pdu), "REPEAT") == 0) {
			logger(LOG_INFO, "received a [REPEAT] request");
			char *k;
			pdu_t *dupe;

			int n = 0;
			for_each_key_value(&dispatcher->pdus, k, dupe) {
				n++;
				logger(LOG_DEBUG, "re-broadcasting %s for %s", pdu_type(dupe), k);
				pdu_send_and_free(pdu_dup(dupe, NULL), dispatcher->broadcast);
			}
			logger(LOG_INFO, "re-broadcast %i PDUs to all connected clients", n);

		} else {
			logger(LOG_ERR, "received unrecognized [%s] PDU on listener", pdu_type(pdu));
		}

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

	zmq_close(dispatcher->broadcast);
	zmq_close(dispatcher->listen);
	zmq_close(dispatcher->subscriber);
	zmq_close(dispatcher->control);

	reactor_free(dispatcher->reactor);
	free(dispatcher);

	logger(LOG_DEBUG, "dispatcher: terminated");
	return NULL;
}
/* }}} */
static int dispatcher_thread(void *zmq, const char *endpoint, const char *listen, const char *broadcast) /* {{{ */
{
	assert(zmq != NULL);
	assert(endpoint != NULL);
	assert(listen != NULL);
	assert(broadcast != NULL);

	int rc;

	logger(LOG_INFO, "initializing dispatcher thread");

	dispatcher_t *dispatcher = vmalloc(sizeof(dispatcher_t));

	logger(LOG_DEBUG, "connecting dispatcher.control -> supervisor");
	rc = subscriber_connect_supervisor(zmq, &dispatcher->control);
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

	logger(LOG_DEBUG, "binding PULL socket dispatcher.listen to %s", listen);
	dispatcher->listen = zmq_socket(zmq, ZMQ_PULL);
	if (!dispatcher->listen)
		return -1;
	rc = zmq_bind(dispatcher->listen, listen);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "binding PUB socket dispatcher.broadcast to %s", broadcast);
	dispatcher->broadcast = zmq_socket(zmq, ZMQ_PUB);
	if (!dispatcher->broadcast)
		return -1;
	rc = zmq_bind(dispatcher->broadcast, broadcast);
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

	logger(LOG_DEBUG, "dispatcher: registering dispatcher.listen with event reactor");
	rc = reactor_set(dispatcher->reactor, dispatcher->listen, _dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: spinning up thread");
	pthread_t tid;
	rc = pthread_create(&tid, NULL, _dispatcher_thread, dispatcher);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

/**************************/

int cmd_cache(int off, int argc, char **argv) /* {{{ */
{
	struct {
		char *endpoint;
		char *listen;
		char *broadcast;

		int   verbose;
		int   daemonize;

		char *pidfile;
		char *user;
		char *group;

	} OPTIONS = {
		.verbose   = 0,
		.endpoint  = strdup("tcp://127.0.0.1:2997"),
		.listen    = strdup("tcp://127.0.0.1:2898"),
		.broadcast = strdup("tcp://127.0.0.1:2897"),
		.daemonize = 1,
		.pidfile   = strdup("/var/run/bolo/cache.pid"),
		.user      = strdup("root"),
		.group     = strdup("root"),
	};

	struct option long_opts[] = {
		{ "verbose",          no_argument, NULL, 'v' },
		{ "endpoint",   required_argument, NULL, 'e' },
		{ "broadcast",  required_argument, NULL, 'B' },
		{ "listen",     required_argument, NULL, 'l' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "pidfile",    required_argument, NULL, 'p' },
		{ "user",       required_argument, NULL, 'u' },
		{ "group",      required_argument, NULL, 'g' },
		{ 0, 0, 0, 0 },
	};

	optind = ++off;
	for (;;) {
		int c = getopt_long(argc, argv, "v+e:Fp:u:g:S:P:l:B:", long_opts, &off);
		if (c == -1) break;

		switch (c) {
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

		case 'l':
			free(OPTIONS.listen);
			OPTIONS.listen = strdup(optarg);
			break;

		case 'B':
			free(OPTIONS.broadcast);
			OPTIONS.broadcast = strdup(optarg);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	if (OPTIONS.daemonize) {
		log_open("bolo-cache", "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			exit(3);
		}
		umask(um);
	} else {
		log_open("bolo-cache", "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");


	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		exit(1);
	}

	int rc;
	rc = subscriber_init();
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize subscriber architecture");
		exit(2);
	}

	rc = dispatcher_thread(zmq, OPTIONS.endpoint, OPTIONS.listen, OPTIONS.broadcast);
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize dispatcher: %s", strerror(errno));
		exit(2);
	}

	subscriber_supervisor(zmq);
	logger(LOG_INFO, "shutting down");

	free(OPTIONS.endpoint);

	free(OPTIONS.pidfile);
	free(OPTIONS.user);
	free(OPTIONS.group);

	return 0;
}
/* }}} */
