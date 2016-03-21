/*
  Copyright 2016 Dan Molik <dan@d3fy.net>

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

static struct {
	char     *endpoint;
	char     *pattern;
	uint16_t  payload;
	uint8_t   ignore;
} OPTIONS = { 0 };

int main(int argc, char **argv)
{
	OPTIONS.endpoint = NULL;
	OPTIONS.pattern  = NULL;
	OPTIONS.payload  = 0;
	OPTIONS.ignore   = 0;

	struct option long_opts[] = {
		{ "help",           no_argument, 0, 'h' },
		{ "endpoint", required_argument, 0, 'e' },
		{ "type",     required_argument, 0, 't' },
		{ "ignore",         no_argument, 0, 'i' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?e:t:+i", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf("bolo_forget v%s\n", BOLO_VERSION);
			printf("Usage: stat_bolo [-?h] [-e tcp://host:port] [-t type] pattern\n");
			printf("Options:\n");
			printf("  pattern              pcre string to match\n");
			printf("  -?, -h, --help       show this help screen\n");
			printf("  -e, --endpoint       bolo endpoint to connect to\n");
			printf("  -t, --type           bolo datapoint types to forget\n");
			printf("  -i, --ignore         ignore future submissions of matched datapoints\n");
			exit(0);

		case 'e':
			free(OPTIONS.endpoint);
			OPTIONS.endpoint = strdup(optarg);
			break;

		case 't':
			if (strcasecmp(optarg, "all") == 0) {
				OPTIONS.payload |= PAYLOAD_ALL;
			} else if (strcasecmp(optarg, "state") == 0) {
				OPTIONS.payload |= PAYLOAD_STATE;
			} else if (strcasecmp(optarg, "counter") == 0) {
				OPTIONS.payload |= PAYLOAD_COUNTER;
			} else if (strcasecmp(optarg, "sample") == 0) {
				OPTIONS.payload |= PAYLOAD_SAMPLE;
			} else if (strcasecmp(optarg, "rate") == 0) {
				OPTIONS.payload |= PAYLOAD_RATE;
			} else {
				fprintf(stderr, "invalid type '%s'\n", optarg);
				exit(1);
			}
			break;

		case 'i':
			OPTIONS.ignore = 1;
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	if (optind > 1) {
		free(OPTIONS.pattern);
		OPTIONS.pattern = strdup(argv[optind]);
	} else {
		fprintf(stderr, "A forget pattern is required\n");
		return 1;
	}

	if (!OPTIONS.payload)
		OPTIONS.payload = PAYLOAD_ALL;

	if (!OPTIONS.endpoint) {
		free(OPTIONS.endpoint);
		OPTIONS.endpoint = strdup("tcp://127.0.0.1:2998");
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
	if (vx_vzmq_connect(z, OPTIONS.endpoint) != 0) {
		fprintf(stderr, "failed to connect to %s\n", OPTIONS.endpoint);
		return 3;
	}

	pdu_t *p = forget_pdu(OPTIONS.payload, OPTIONS.pattern, OPTIONS.ignore);
	if (pdu_send_and_free(p, z) != 0) {
		fprintf(stderr, "failed to send [SET.KEYS] PDU to %s; command aborted\n", OPTIONS.endpoint);
		return 3;
	}

	p = pdu_recv(z);
	if (!p) {
		fprintf(stderr, "no response received from %s\n", OPTIONS.endpoint);
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
