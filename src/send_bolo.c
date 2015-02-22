#include "bolo.h"
#include <getopt.h>

static struct {
	char *endpoint;
	int   verbose;
	int   type;
} OPTIONS = { 0 };

#define DEBUG OPTIONS.verbose > 0

#define TYPE_STREAM  0
#define TYPE_STATE   1
#define TYPE_COUNTER 2
#define TYPE_SAMPLE  3

static pdu_t* state_pdu(int argc, char **argv, char *ts)
{
	if (argc < 3)
		return NULL;

	int status = UNKNOWN;
	char *code = argv[1];
	if (strcasecmp(code, "0")    == 0
	 || strcasecmp(code, "ok")   == 0
	 || strcasecmp(code, "okay") == 0)
		status = OK;

	else
	if (strcasecmp(code, "1")  == 0
	 || strcasecmp(code, "warn")    == 0
	 || strcasecmp(code, "warning") == 0)
		status = WARNING;

	else
	if (strcasecmp(code, "2")        == 0
	 || strcasecmp(code, "crit")     == 0
	 || strcasecmp(code, "critical") == 0)
		status = CRITICAL;

	strings_t *parts = strings_new(argv + 2);
	char *msg  = strings_join(parts, " ");
	strings_free(parts);

	pdu_t *pdu = pdu_make("STATE", 0);
	if (ts) pdu_extendf(pdu, "%s", ts);
	else    pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", argv[0]);
	pdu_extendf(pdu, "%u", status);
	pdu_extendf(pdu, "%s", msg);
	if (DEBUG) fprintf(stderr, "+>> built PDU [%s|%i|%s|%u|%s]\n",
		pdu_type(pdu), time_s(), argv[optind + 0], status, msg);
	free(msg);

	return pdu;
}

static pdu_t* counter_pdu(int argc, char **argv, char *ts)
{
	if (argc < 1)
		return NULL;

	int incr = 1;
	if (argc == 2 && argv[1])
		incr = atoi(argv[1]);

	pdu_t *pdu = pdu_make("COUNTER", 0);
	if (ts) pdu_extendf(pdu, "%s", ts);
	else    pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", argv[0]);
	pdu_extendf(pdu, "%u", incr);
	if (DEBUG) fprintf(stderr, "+>> built PDU [%s|%i|%s|%u]\n",
		pdu_type(pdu), time_s(), argv[0], incr);

	return pdu;
}

static pdu_t* sample_pdu(int argc, char **argv, char *ts)
{
	if (argc < 2)
		return NULL;

	pdu_t *pdu = pdu_make("SAMPLE", 0);
	if (ts) pdu_extendf(pdu, "%s", ts);
	else    pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", argv[0]);
	pdu_extendf(pdu, "%u", argc - 1);
	if (DEBUG) fprintf(stderr, "+>> built PDU [%s|%i|%s|%u",
		pdu_type(pdu), time_s(), argv[0], argc - 1);

	int i;
	for (i = 1; i <= argc; i++) {
		pdu_extendf(pdu, "%s", argv[i]);
		if (DEBUG) fprintf(stderr, "|%s", argv[i]);
	}
	if (DEBUG) fprintf(stderr, "]\n");

	return pdu;
}

static int send_pdu(void *z, pdu_t *pdu)
{
	if (pdu_send_and_free(pdu, z) != 0) {
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

	return 0;
}

int main(int argc, char **argv)
{
	OPTIONS.verbose = 0;
	OPTIONS.endpoint = strdup("tcp://127.0.0.1:2999");

	struct option long_opts[] = {
		{ "help",           no_argument, 0, 'h' },
		{ "verbose",        no_argument, 0, 'v' },
		{ "endpoint", required_argument, 0, 'e' },
		{ "type",     required_argument, 0, 't' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+e:t:", long_opts, &idx);
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

		case 't':
			if (strcasecmp(optarg, "state") == 0) {
				OPTIONS.type = TYPE_STATE;

			} else if (strcasecmp(optarg, "counter") == 0) {
				OPTIONS.type = TYPE_COUNTER;

			} else if (strcasecmp(optarg, "sample") == 0) {
				OPTIONS.type = TYPE_SAMPLE;

			} else if (strcasecmp(optarg, "stream") == 0) {
				OPTIONS.type = TYPE_STREAM;

			} else {
				fprintf(stderr, "invalid type '%s'\n", optarg);
				return 1;
			}
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	pdu_t *pdu;

	if (OPTIONS.type == TYPE_STATE) {
		pdu = state_pdu(argc - optind, argv + optind, NULL);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t state name code message\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_COUNTER) {
		pdu = counter_pdu(argc - optind, argv + optind, NULL);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t counter name [increment]\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_SAMPLE) {
		pdu = sample_pdu(argc - optind, argv + optind, NULL);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t sample name value [value ...]\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_STREAM) {
		if (argc - optind != 0) {
			fprintf(stderr, "USAGE: %s -t stream < input.file\n", argv[0]);
			return 1;
		}

	} else {
		fprintf(stderr, "USAGE: %s -t (sample|counter|state) args\n", argv[0]);
		return 1;
	}

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

	int rc;
	if (pdu) {
		rc = send_pdu(z, pdu);

	} else {
		char buf[8192];
		while (fgets(buf, 8192, stdin)) {
			char *nl = strrchr(buf, '\n');
			if (*nl)
				*nl = '\0';

			strings_t *l = strings_split(buf, strlen(buf), " ", SPLIT_NORMAL);
			if (strcmp(l->strings[0], "STATE") == 0) {
				pdu = state_pdu(l->num - 2, l->strings + 2, l->strings[1]);

			} else if (strcmp(l->strings[0], "COUNTER") == 0) {
				pdu = counter_pdu(l->num - 2, l->strings + 2, l->strings[1]);

			} else if (strcmp(l->strings[0], "SAMPLE") == 0) {
				pdu = sample_pdu(l->num - 2, l->strings + 2, l->strings[1]);

			} else {
				pdu = NULL;
				if (DEBUG)
					fprintf(stderr, "+>> unrecognized line: '%s'\n", buf);
			}
			strings_free(l);

			if (pdu) {
				rc = send_pdu(z, pdu);
				if (rc) break;
			}
		}
	}

	if (DEBUG) fprintf(stderr, "+>> completed successfully.\n");
	return rc;
}
