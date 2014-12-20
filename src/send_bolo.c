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
	OPTIONS.endpoint = strdup("tcp://127.0.0.1:2999");

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

	if (argc - optind < 3) {
		fprintf(stderr, "USAGE: %s [OPTIONS] name code message...\n", argv[0]);
		return 1;
	}

	char *name = argv[optind + 0];
	char *code = argv[optind + 1];
	strings_t *parts = strings_new(optind + argv + 2);
	char *msg  = strings_join(parts, " ");
	strings_free(parts);

	int status = UNKNOWN;
	if (strcasecmp(code, "0") == 0 || strcasecmp(code, "ok") == 0 || strcasecmp(code, "okay") == 0)
		status = OK;
	else if (strcasecmp(code, "1") == 0 || strcasecmp(code, "warn") == 0 || strcasecmp(code, "warning") == 0)
		status = WARNING;
	else if (strcasecmp(code, "2") == 0 || strcasecmp(code, "crit") == 0 || strcasecmp(code, "critical") == 0)
		status = CRITICAL;

	if (DEBUG) {
		fprintf(stderr, "+>> determined name to be '%s'\n", name);
		fprintf(stderr, "+>> determined status to be %i (from %s)\n", status, code);
		fprintf(stderr, "+>> determined summary to be '%s'\n", msg);
	}

	if (!name || !*name) {
		fprintf(stderr, "invalid name '%s'\n", name);
		return 2;
	}
	if (!msg || !*msg) {
		fprintf(stderr, "invalid message '%s'\n", msg);
		return 2;
	}
	code = string("%u", status);

	if (DEBUG) fprintf(stderr, "+>> allocating 0MQ context\n");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		fprintf(stderr, "failed to initialize 0MQ context; aborting (results NOT submitted)\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> allocating 0MQ DEALER socket to talk to %s\n", OPTIONS.endpoint);
	void *z = zmq_socket(zmq, ZMQ_DEALER);
	if (!z) {
		fprintf(stderr, "failed to create a DEALER socket; aborting (results NOT submitted)\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> connecting to %s\n", OPTIONS.endpoint);
	if (zmq_connect(z, OPTIONS.endpoint) != 0) {
		fprintf(stderr, "failed to connect to %s; aborting (results NOT submitted)\n", OPTIONS.endpoint);
		return 3;
	}

	if (DEBUG) fprintf(stderr, "+>> sending [SUBMIT|%s|%s|%s] PDU\n", name, code, msg);
	if (pdu_send_and_free(pdu_make("SUBMIT", 3, name, code, msg), z) != 0) {
		fprintf(stderr, "failed to send results to %s\n", OPTIONS.endpoint);
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> awaiting response PDU...\n");
	pdu_t *a = pdu_recv(z);
	if (!a) {
		fprintf(stderr, "no response received from %s, assume the worst.\n", OPTIONS.endpoint);
		return 4;
	}
	if (DEBUG) fprintf(stderr, "+>> received a [%s] PDU in response\n", pdu_type(a));
	if (strcmp(pdu_type(a), "ERROR") == 0) {
		fprintf(stderr, "error: %s\n", pdu_string(a, 1));
		return 4;
	}
	if (strcmp(pdu_type(a), "OK") != 0) {
		fprintf(stderr, "unknown response [%s] from %s\n", pdu_type(a), OPTIONS.endpoint);
		return 4;
	}

	if (DEBUG) fprintf(stderr, "+>> completed successfully.\n");
	return 0;
}
