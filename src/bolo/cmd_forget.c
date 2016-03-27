#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <bolo.h>
#include <vigor.h>

int cmd_forget(int off, int argc, char **argv)
{
	char     *endpoint = NULL;
	char     *pattern  = NULL;
	uint16_t  payload  = 0;
	uint8_t   ignore   = 0;

	struct option long_opts[] = {
		{ "endpoint", required_argument, 0, 'e' },
		{ "type",     required_argument, 0, 't' },
		{ "ignore",         no_argument, 0, 'i' },
		{ 0, 0, 0, 0 },
	};

	optind = ++off;
	for (;;) {
		int c = getopt_long(argc, argv, "e:t:+i", long_opts, &off);
		if (c == -1) break;

		switch (c) {
		case 'e':
			free(endpoint);
			endpoint = strdup(optarg);
			break;

		case 't':
			if (strcasecmp(optarg, "all") == 0) {
				payload |= PAYLOAD_ALL;
			} else if (strcasecmp(optarg, "state") == 0) {
				payload |= PAYLOAD_STATE;
			} else if (strcasecmp(optarg, "counter") == 0) {
				payload |= PAYLOAD_COUNTER;
			} else if (strcasecmp(optarg, "sample") == 0) {
				payload |= PAYLOAD_SAMPLE;
			} else if (strcasecmp(optarg, "rate") == 0) {
				payload |= PAYLOAD_RATE;
			} else {
				fprintf(stderr, "invalid type '%s'\n", optarg);
				return 1;
			}
			break;

		case 'i':
			ignore = 1;
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (optind > 1) {
		free(pattern);
		pattern = strdup(argv[optind]);
	} else {
		fprintf(stderr, "A forget pattern is required\n");
		return 1;
	}

	if (!payload)
		payload = PAYLOAD_ALL;

	if (!endpoint) {
		free(endpoint);
		endpoint = strdup("tcp://127.0.0.1:2998");
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

	pdu_t *p = forget_pdu(payload, pattern, ignore);
	if (pdu_send_and_free(p, z) != 0) {
		fprintf(stderr, "failed to send [FORGET] PDU to %s; command aborted\n", endpoint);
		return 3;
	}

	p = pdu_recv(z);
	if (!p) {
		fprintf(stderr, "no response received from %s\n", endpoint);
		return 3;
	}

	if (strcmp(pdu_type(p), "ERROR") == 0) {
		fprintf(stderr, "error: %s\n", pdu_string(p, 1));
		pdu_free(p);
		return 1;
	}

	fprintf(stdout, "OK\n");
	return 0;
}
