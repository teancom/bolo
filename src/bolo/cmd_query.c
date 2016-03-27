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
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <vigor.h>

int cmd_query(int off, int argc, char **argv)
{
	char *endpoint = strdup("tcp://127.0.0.1:2998");
	struct option long_opts[] = {
		{ "endpoint", required_argument, 0, 'e' },
		{ 0, 0, 0, 0 },
	};

	optind = ++off;
	for (;;) {
		int c = getopt_long(argc, argv, "e:", long_opts, &off);
		if (c == -1) break;

		switch (c) {
		case 'e':
			free(endpoint);
			endpoint = strdup(optarg);
		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	void *zmq = zmq_ctx_new();
	if (!zmq) {
		fprintf(stderr, "failed to initialize 0MQ context\n");
		return 3;
	}
	void *z = zmq_socket(zmq, ZMQ_DEALER);
	if (!z) {
		fprintf(stderr, "failed to create a DEALER socket\n");
		return 3;
	}
	if (vzmq_connect(z, endpoint) != 0) {
		fprintf(stderr, "failed to connect to %s\n", endpoint);
		return 3;
	}

	pdu_t *p;
	char *a, *b, *c, *s;
	char line[8192];
	for (;;) {
		if (isatty(0)) fprintf(stderr, "> ");
		if (fgets(line, 8192, stdin) == NULL)
			break;

		a = line;
		while (*a && isspace(*a)) a++;
		if (!*a || *a == '#') continue;
		b = a;
		while (*b && !isspace(*b)) b++;

		c = b;
		if (*c) c++;
		*b = '\0';

		if (strcasecmp(a, "stat") == 0) {
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

			if (pdu_send_and_free(pdu_make("GET.STATE", 1, c), z) != 0) {
				fprintf(stderr, "failed to send [GET.STATE] PDU to %s; command aborted\n", endpoint);
				return 3;
			}
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", endpoint);
				return 3;
			}
			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				continue;
			}
			if (strcmp(pdu_type(p), "STATE") != 0) {
				fprintf(stderr, "unknown response [%s] from %s\n", pdu_type(p), endpoint);
				return 4;
			}
			fprintf(stdout, "%s ",  s = pdu_string(p, 1)); free(s); /* name      */
			fprintf(stdout, "%s ",  s = pdu_string(p, 2)); free(s); /* timestamp */
			fprintf(stdout, "%s ",  s = pdu_string(p, 3)); free(s); /* stale     */
			fprintf(stdout, "%s ",  s = pdu_string(p, 4)); free(s); /* code      */
			fprintf(stdout, "%s\n", s = pdu_string(p, 5)); free(s); /* summary   */
			pdu_free(p);

		} else if (strcasecmp(a, "set.keys") == 0) {
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

			if (pdu_send_and_free(p, z) != 0) {
				fprintf(stderr, "failed to send [SET.KEYS] PDU to %s; command aborted\n", endpoint);
				return 3;
			}
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", endpoint);
				return 3;
			}

			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				pdu_free(p);
				continue;
			}
			pdu_free(p);

		} else if (strcasecmp(a, "get.keys") == 0) {
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

			if (pdu_send_and_free(p, z) != 0) {
				fprintf(stderr, "failed to send [GET.KEYS] PDU to %s; command aborted\n", endpoint);
				return 3;
			}
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", endpoint);
				return 3;
			}

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

			if (pdu_send_and_free(p, z) != 0) {
				fprintf(stderr, "failed to send [DEL.KEYS] PDU to %s; command aborted\n", endpoint);
				return 3;
			}
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", endpoint);
				return 3;
			}

			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				pdu_free(p);
				continue;
			}
			pdu_free(p);

		} else if (strcasecmp(a, "search.keys") == 0) {
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

			if (pdu_send_and_free(pdu_make("SEARCH.KEYS", 1, c), z) != 0) {
				fprintf(stderr, "failed to send [SEARCH.KEYS] PDU to %s; command aborted\n", endpoint);
				return 3;
			}
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", endpoint);
				return 3;
			}
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

			if (pdu_send_and_free(pdu_make("GET.EVENTS", 1, ts), z) != 0) {
				fprintf(stderr, "failed to send [GET.EVENTS] PDU to %s; command aborted\n", endpoint);
				return 3;
			}
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", endpoint);
				return 3;
			}
			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				continue;
			}
			if (strcmp(pdu_type(p), "EVENTS") != 0) {
				fprintf(stderr, "unknown response [%s] from %s\n", pdu_type(p), endpoint);
				return 4;
			}
			fprintf(stdout, "%s", s = pdu_string(p, 1)); free(s);
			pdu_free(p);

		} else if (strcasecmp(a, "dump") == 0) {
			if (*c) fprintf(stderr, "ignoring useless arguments to `dump' command\n");

			if (pdu_send_and_free(pdu_make("DUMP", 0), z) != 0) {
				fprintf(stderr, "failed to send [DUMP] PDU to %s; command aborted\n", endpoint);
				return 3;
			}
			p = pdu_recv(z);
			if (!p) {
				fprintf(stderr, "no response received from %s\n", endpoint);
				return 3;
			}
			if (strcmp(pdu_type(p), "ERROR") == 0) {
				fprintf(stderr, "error: %s\n", s = pdu_string(p, 1)); free(s);
				continue;
			}
			if (strcmp(pdu_type(p), "DUMP") != 0) {
				fprintf(stderr, "unknown response [%s] from %s\n", pdu_type(p), endpoint);
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
