#include "bolo.h"
#include <getopt.h>

static struct {
	char *endpoint;
	int   verbose;
} OPTIONS = { 0 };

#define DEBUG OPTIONS.verbose > 0

int main(int argc, char **argv)
{
	OPTIONS.verbose = 0;
	OPTIONS.endpoint = strdup("tcp://127.0.0.1:2998");

	struct option long_opts[] = {
		{ "help",           no_argument, 0, 'h' },
		{ "verbose",        no_argument, 0, 'v' },
		{ "endpoint", required_argument, 0, 'e' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+e:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			break;

		case 'v':
			OPTIONS.verbose++;
			break;

		case 'e':
			free(OPTIONS.endpoint);
			OPTIONS.endpoint = strdup(optarg);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

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
	if (zmq_connect(z, OPTIONS.endpoint) != 0) {
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

			if (DEBUG) fprintf(stderr, "+>> sending [STATE|%s] PDU\n", c);
			if (pdu_send_and_free(pdu_make("STATE", 1, c), z) != 0) {
				fprintf(stderr, "failed to send [STATE] PDU to %s; command aborted\n", OPTIONS.endpoint);
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
