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

#define MODE_QUERY  0
#define MODE_LISTEN 1

static struct {
	char *endpoint;
	int   verbose;
	int   mode;
} OPTIONS = { 0 };

#define DEBUG OPTIONS.verbose > 0

static int stat_listenmode(void);
static int stat_querymode(void);

int main(int argc, char **argv)
{
	OPTIONS.verbose  = 0;
	OPTIONS.endpoint = NULL;
	OPTIONS.mode     = MODE_QUERY;

	struct option long_opts[] = {
		{ "help",           no_argument, 0, 'h' },
		{ "verbose",        no_argument, 0, 'v' },
		{ "endpoint", required_argument, 0, 'e' },
		{ "listen",         no_argument, 0, 'l' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+e:l", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			logger(LOG_DEBUG, "handling -h/-?/--help");
			printf("stat_bolo v%s\n", BOLO_VERSION);
			printf("Usage: stat_bolo [-?hVvl] [-e tcp://host:port]\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -l, --listen         print all broadcast events\n");
			printf("  -e, --endpoint       bolo endpoint to connect to\n");
			exit(0);

		case 'V':
			logger(LOG_DEBUG, "handling -V/--version");
			printf("stat_bolo v%s\n"
			       "Copyright (C) 2015 James Hunt\n",
			       BOLO_VERSION);
			exit(0);

		case 'v':
			logger(LOG_DEBUG, "handling -v/--verbose");
			OPTIONS.verbose++;
			break;

		case 'e':
			logger(LOG_DEBUG, "handling -e/--endpoint; changing from '%s' to '%s'",
					OPTIONS.endpoint, optarg);
			free(OPTIONS.endpoint);
			OPTIONS.endpoint = strdup(optarg);
			break;

		case 'l':
			logger(LOG_DEBUG, "handling -l/--listen");
			OPTIONS.mode = MODE_LISTEN;
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	switch (OPTIONS.mode) {
	case MODE_LISTEN: return stat_listenmode();
	default:          return stat_querymode();
	}
}

static int stat_listenmode(void)
{
	if (!OPTIONS.endpoint)
		OPTIONS.endpoint = strdup("tcp://127.0.0.1:2997");

	if (DEBUG) fprintf(stderr, "+>> allocating 0MQ context\n");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		fprintf(stderr, "failed to initialize 0MQ context\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> allocating 0MQ SUB socket to talk to %s\n", OPTIONS.endpoint);
	void *z = zmq_socket(zmq, ZMQ_SUB);
	if (!z) {
		fprintf(stderr, "failed to create a SUB socket\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> setting subscriber filter\n");
	if (zmq_setsockopt(z, ZMQ_SUBSCRIBE, "", 0) != 0) {
		fprintf(stderr, "failed to set subscriber filter\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> connecting to %s\n", OPTIONS.endpoint);
	if (vx_vzmq_connect(z, OPTIONS.endpoint) != 0) {
		fprintf(stderr, "failed to connect to %s\n", OPTIONS.endpoint);
		return 3;
	}

	signal_handlers();
	while (!signalled()) {
		if (DEBUG) fprintf(stderr, "+>> waiting for a broadcast PDU\n");

		ssize_t i;
		char *s;
		pdu_t *p;

		while ((p = pdu_recv(z))) {
			if (DEBUG) fprintf(stderr, "+>> got a [%s] PDU of %i frames\n", pdu_type(p), pdu_size(p));
			printf("%s", pdu_type(p));
			for (i = 1; i < pdu_size(p); i++) {
				printf(" %s", s = pdu_string(p, i));
				free(s);
			}
			printf("\n");
			pdu_free(p);

		}
	}

	if (DEBUG) fprintf(stderr, "+>> terminating\n");
	return 0;
}

static int stat_querymode(void)
{
	if (!OPTIONS.endpoint)
		OPTIONS.endpoint = strdup("tcp://127.0.0.1:2998");

	if (DEBUG) fprintf(stderr, "+>> allocating 0MQ context\n");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		fprintf(stderr, "failed to initialize 0MQ context\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> allocating 0MQ DEALER socket to talk to %s\n", OPTIONS.endpoint);
	void *z = zmq_socket(zmq, ZMQ_DEALER);
	if (!z) {
		fprintf(stderr, "failed to create a DEALER socket\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> connecting to %s\n", OPTIONS.endpoint);
	if (vx_vzmq_connect(z, OPTIONS.endpoint) != 0) {
		fprintf(stderr, "failed to connect to %s\n", OPTIONS.endpoint);
		return 3;
	}

	pdu_t *p;
	char *a, *b, *c, *s;
	char line[8192];
	for (;;) {
		if (isatty(0)) fprintf(stderr, "> ");
		if (fgets(line, 8192, stdin) == NULL)
			break;
		if (DEBUG) fprintf(stderr, "+>> read line: %s", line);

		a = line;
		while (*a && isspace(*a)) a++;
		if (!*a || *a == '#') continue;
		b = a;
		while (*b && !isspace(*b)) b++;

		c = b;
		if (*c) c++;
		*b = '\0';

		if (DEBUG) fprintf(stderr, "+>> parsed command as '%s'\n", a);
		if (strcasecmp(a, "stat") == 0) {
			if (DEBUG) fprintf(stderr, "+>> arguments remaining: %s\n", c);
			while (*c && isspace(*c)) c++;
			if (!*c) {
				fprintf(stderr, "missing state name to `stat' call\n");
				fprintf(stderr, "usage: stat <state-name>\n");
				continue;
			}

			a = c; while (*a && !isspace(*a)) a++;
			b = a; while (*b &&  isspace(*b)) b++;
			*a = '\0';
			if (*b) {
				fprintf(stderr, "too many arguments to `stat' call\n");
				fprintf(stderr, "usage: stat <state-name>\n");
				continue;
			}
			if (DEBUG) fprintf(stderr, "+>> parsed `stat' agument as '%s'\n", c);

			if (DEBUG) fprintf(stderr, "+>> sending [GET.STATE|%s] PDU\n", c);
			if (pdu_send_and_free(pdu_make("GET.STATE", 1, c), z) != 0) {
				fprintf(stderr, "failed to send [GET.STATE] PDU to %s; command aborted\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> awaiting response PDU...\n");
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> received a [%s] PDU in response\n", pdu_type(p));
			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				continue;
			}
			if (strcmp(pdu_type(p), "STATE") != 0) {
				fprintf(stderr, "unknown response [%s] from %s\n", pdu_type(p), OPTIONS.endpoint);
				return 4;
			}
			fprintf(stdout, "%s ",  s = pdu_string(p, 1)); free(s); /* name      */
			fprintf(stdout, "%s ",  s = pdu_string(p, 2)); free(s); /* timestamp */
			fprintf(stdout, "%s ",  s = pdu_string(p, 3)); free(s); /* stale     */
			fprintf(stdout, "%s ",  s = pdu_string(p, 4)); free(s); /* code      */
			fprintf(stdout, "%s\n", s = pdu_string(p, 5)); free(s); /* summary   */
			pdu_free(p);

		} else if (strcasecmp(a, "set.keys") == 0) {
			if (DEBUG) fprintf(stderr, "+>> arguments remaining: %s\n", c);
			while (*c && isspace(*c)) c++;
			if (!*c) {
				fprintf(stderr, "missing arguments to `set.keys' call\n");
				fprintf(stderr, "usage: set.keys key1 value1 key2 value2 ...\n");
				continue;
			}

			int n = 0;
			p = pdu_make("SET.KEYS", 0);
			while (*c) {
				a = c; while (*a && !isspace(*a)) a++;
				b = a; while (*b &&  isspace(*b)) b++;
				*a = '\0';

				n++;
				pdu_extendf(p, "%s", a);
				c = b;
			}

			if (n % 2 != 0) {
				fprintf(stderr, "odd number of arguments to `set.keys' call\n");
				fprintf(stderr, "usage: set.keys key1 value1 key2 value2 ...\n");
				pdu_free(p);
				continue;
			}

			if (DEBUG) fprintf(stderr, "+>> sending [SET.KEYS] PDU\n");
			if (pdu_send_and_free(p, z) != 0) {
				fprintf(stderr, "failed to send [SET.KEYS] PDU to %s; command aborted\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> awaiting response PDU...\n");
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> received a [%s] PDU in response\n", pdu_type(p));

			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				pdu_free(p);
				continue;
			}
			pdu_free(p);

		} else if (strcasecmp(a, "get.keys") == 0) {
			if (DEBUG) fprintf(stderr, "+>> arguments remaining: %s\n", c);
			while (*c && isspace(*c)) c++;
			if (!*c) {
				fprintf(stderr, "missing arguments to `get.keys' call\n");
				fprintf(stderr, "usage: get.keys key1 key2 ...\n");
				continue;
			}

			p = pdu_make("GET.KEYS", 0);
			while (*c) {
				a = c; while (*a && !isspace(*a)) a++;
				b = a; while (*b &&  isspace(*b)) b++;
				*a = '\0';

				pdu_extendf(p, "%s", a);
				c = b;
			}

			if (DEBUG) fprintf(stderr, "+>> sending [GET.KEYS] PDU\n");
			if (pdu_send_and_free(p, z) != 0) {
				fprintf(stderr, "failed to send [GET.KEYS] PDU to %s; command aborted\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> awaiting response PDU...\n");
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> received a [%s] PDU in response\n", pdu_type(p));

			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				pdu_free(p);
				continue;
			}

			int i;
			for (i = 1; i < pdu_size(p); i += 2) {
				a = pdu_string(p, i);
				b = pdu_string(p, i + 1);

				fprintf(stdout, "%s = %s\n", a, b);

				free(a);
				free(b);
			}
			pdu_free(p);

		} else if (strcasecmp(a, "del.keys") == 0) {
			if (DEBUG) fprintf(stderr, "+>> arguments remaining: %s\n", c);
			while (*c && isspace(*c)) c++;
			if (!*c) {
				fprintf(stderr, "missing argument to `del.keys' call\n");
				fprintf(stderr, "usage: del.keys key1 key2 ...\n");
				continue;
			}

			p = pdu_make("DEL.KEYS", 0);
			while (*c) {
				a = c; while (*a && !isspace(*a)) a++;
				b = a; while (*b &&  isspace(*b)) b++;
				*a = '\0';

				pdu_extendf(p, "%s", a);
				c = b;
			}

			if (DEBUG) fprintf(stderr, "+>> sending [DEL.KEYS] PDU\n");
			if (pdu_send_and_free(p, z) != 0) {
				fprintf(stderr, "failed to send [DEL.KEYS] PDU to %s; command aborted\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> awaiting response PDU...\n");
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> received a [%s] PDU in response\n", pdu_type(p));

			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				pdu_free(p);
				continue;
			}
			pdu_free(p);

		} else if (strcasecmp(a, "search.keys") == 0) {
			if (DEBUG) fprintf(stderr, "+>> arguments remaining: %s\n", c);
			while (*c && isspace(*c)) c++;
			if (!*c) {
				fprintf(stderr, "missing pattern argument to `search.keys' call\n");
				fprintf(stderr, "usage: search.keys <pattern>\n");
				continue;
			}

			a = c; while (*a && !isspace(*a)) a++;
			b = a; while (*b &&  isspace(*b)) b++;
			*a = '\0';
			if (*b) {
				fprintf(stderr, "too many arguments to `search.keys' call\n");
				fprintf(stderr, "usage: search.keys <pattern>\n");
				continue;
			}

			if (DEBUG) fprintf(stderr, "+>> sending [SEARCH.KEYS] PDU\n");
			if (pdu_send_and_free(pdu_make("SEARCH.KEYS", 1, c), z) != 0) {
				fprintf(stderr, "failed to send [SEARCH.KEYS] PDU to %s; command aborted\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> awaiting response PDU...\n");
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> received a [%s] PDU in response\n", pdu_type(p));
			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				pdu_free(p);
				continue;
			}

			int i;
			for (i = 1; i < pdu_size(p); i++) {
				fprintf(stdout, "%s\n", s = pdu_string(p, i));
				free(s);
			}
			pdu_free(p);

		} else if (strcasecmp(a, "get.events") == 0) {
			char *ts = "0";
			if (*c) {
				ts = c;
				while (*c && isdigit(*c)) c++;
				*c = '\0';
			}

			if (DEBUG) fprintf(stderr, "+>> sending [GET.EVENTS] PDU\n");
			if (pdu_send_and_free(pdu_make("GET.EVENTS", 1, ts), z) != 0) {
				fprintf(stderr, "failed to send [GET.EVENTS] PDU to %s; command aborted\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> awauting response PDU...\n");
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> received a [%s] PDU in response\n", pdu_type(p));
			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				continue;
			}
			if (strcmp(pdu_type(p), "EVENTS") != 0) {
				fprintf(stderr, "unknown response [%s] from %s\n", pdu_type(p), OPTIONS.endpoint);
				return 4;
			}
			fprintf(stdout, "%s", s = pdu_string(p, 1)); free(s);
			pdu_free(p);

		} else if (strcasecmp(a, "dump") == 0) {
			if (*c) fprintf(stderr, "ignoring useless arguments to `dump' command\n");

			if (DEBUG) fprintf(stderr, "+>> sending [DUMP] PDU\n");
			if (pdu_send_and_free(pdu_make("DUMP", 0), z) != 0) {
				fprintf(stderr, "failed to send [DUMP] PDU to %s; command aborted\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> awaiting response PDU...\n");
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", OPTIONS.endpoint);
				return 3;
			}
			if (DEBUG) fprintf(stderr, "+>> received a [%s] PDU in response\n", pdu_type(p));
			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				continue;
			}
			if (strcmp(pdu_type(p), "DUMP") != 0) {
				fprintf(stderr, "unknown response [%s] from %s\n", pdu_type(p), OPTIONS.endpoint);
				return 4;
			}
			fprintf(stdout, "%s", s = pdu_string(p, 1)); free(s);
			pdu_free(p);

		} else {
			fprintf(stderr, "unrecognized command '%s'\n", a);
			continue;
		}
	}
	return 0;
}
