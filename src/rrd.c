#include "bolo.h"
#include <getopt.h>

static struct {
	char *endpoint;
	int   verbose;
} OPTIONS = { 0 };

#define DEBUG OPTIONS.verbose > 0

int main(int argc, char **argv)
{
	OPTIONS.verbose  = 0;
	OPTIONS.endpoint = NULL;

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
	if (zmq_connect(z, OPTIONS.endpoint) != 0) {
		fprintf(stderr, "failed to connect to %s\n", OPTIONS.endpoint);
		return 3;
	}

	char *s;
	pdu_t *p;
	while ((p = pdu_recv(z))) {
		printf("%s", pdu_type(p));
		size_t i = 1;
		for (;;) {
			if (!(s = pdu_string(p, i++))) break;
			printf(" %s", s); free(s);
		}
		printf("\n");
		pdu_free(p);
	}

	return 0;
}
