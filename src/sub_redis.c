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
#include <hiredis/hiredis.h>

static struct {
	char *endpoint;

	int   verbose;
	int   daemonize;

	char *redis_host;
	int   redis_port;

	char *pidfile;
	char *user;
	char *group;
} OPTIONS = { 0 };

int main(int argc, char **argv)
{
	OPTIONS.verbose    = 0;
	OPTIONS.endpoint   = strdup("tcp://127.0.0.1:2997");
	OPTIONS.daemonize  = 1;
	OPTIONS.pidfile    = strdup("/var/run/bolo2redis.pid");
	OPTIONS.user       = strdup("root");
	OPTIONS.group      = strdup("root");
	OPTIONS.redis_host = strdup("127.0.0.1");
	OPTIONS.redis_port = 6379;

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "version",          no_argument, NULL, 'V' },
		{ "verbose",          no_argument, NULL, 'v' },
		{ "endpoint",   required_argument, NULL, 'e' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "pidfile",    required_argument, NULL, 'p' },
		{ "user",       required_argument, NULL, 'u' },
		{ "group",      required_argument, NULL, 'g' },
		{ "host",       required_argument, NULL, 'H' },
		{ "port",       required_argument, NULL, 'P' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:Fp:u:g:H:P:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf("bolo2redis v%s\n", BOLO_VERSION);
			printf("Usage: bolo2redis [-h?FVv] [-e tcp://host:port]\n"
			       "                  [-H redis.host.or.ip] [-P port]\n"
			       "                  [-u user] [-g group] [-p /path/to/pidfile]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -F, --foreground     don't daemonize, stay in the foreground\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -H, --host           name or address of redis server\n");
			printf("  -P, --port           what port redis is running on\n");
			printf("  -u, --user           user to run as (if daemonized)\n");
			printf("  -g, --group          group to run as (if daemonized)\n");
			printf("  -p, --pidfile        where to store the pidfile (if daemonized)\n");
			exit(0);

		case 'V':
			logger(LOG_DEBUG, "handling -V/--version");
			printf("bolo2redis v%s\n"
			       "Copyright (C) 2015 James Hunt\n",
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

		case 'H':
			free(OPTIONS.redis_host);
			OPTIONS.redis_host = strdup(optarg);
			break;

		case 'P':
			OPTIONS.redis_port = atoi(optarg);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (OPTIONS.daemonize) {
		log_open("bolo2redis", "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			return 3;
		}
		umask(um);
	} else {
		log_open("bolo2redis", "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");

	logger(LOG_DEBUG, "allocating 0MQ context");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		return 3;
	}
	logger(LOG_DEBUG, "allocating 0MQ SUB socket to talk to %s", OPTIONS.endpoint);
	void *z = zmq_socket(zmq, ZMQ_SUB);
	if (!z) {
		logger(LOG_ERR, "failed to create a SUB socket");
		return 3;
	}
	logger(LOG_DEBUG, "setting subscriber filter");
	if (zmq_setsockopt(z, ZMQ_SUBSCRIBE, "", 0) != 0) {
		logger(LOG_ERR, "failed to set subscriber filter");
		return 3;
	}
	logger(LOG_DEBUG, "connecting to %s", OPTIONS.endpoint);
	if (vx_vzmq_connect(z, OPTIONS.endpoint) != 0) {
		logger(LOG_ERR, "failed to connect to %s", OPTIONS.endpoint);
		return 3;
	}

	logger(LOG_INFO, "connecting to redis at %s:%i", OPTIONS.redis_host, OPTIONS.redis_port);
	redisContext *redis = redisConnect(OPTIONS.redis_host, OPTIONS.redis_port);
	if (redis != NULL && redis->err) {
		logger(LOG_ERR, "failed to connect to redis running at %s:%i: %s",
				OPTIONS.redis_host, OPTIONS.redis_port, redis->err);
		return 3;
	}

	pdu_t *p;
	logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);

	signal_handlers();
	while (!signalled()) {
		while ((p = pdu_recv(z))) {
			logger(LOG_INFO, "received a [%s] PDU of %i frames", pdu_type(p), pdu_size(p));

			if (strcmp(pdu_type(p), "SET.KEYS") == 0 && pdu_size(p) % 2 == 1 ) {
				int i = 1;
				while (i < pdu_size(p)) {
					char *k = pdu_string(p, i++);
					char *v = pdu_string(p, i++);
					logger(LOG_DEBUG, "setting key `%s' = '%s'", k, v);

					redisReply *reply = redisCommand(redis, "SET %s %s", k, v);
					if (reply->type == REDIS_REPLY_ERROR) {
						logger(LOG_ERR, "received error from redis: %s", reply->str);
					}

					free(k);
					free(v);
				}
			}

			pdu_free(p);
			logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);
		}
	}

	logger(LOG_INFO, "shutting down");
	vzmq_shutdown(z, 0);
	zmq_ctx_destroy(zmq);
	return 0;
}
