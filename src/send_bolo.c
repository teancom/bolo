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
#define TYPE_KEY     4
#define TYPE_EVENT   5
#define TYPE_RATE    6

static int send_pdu(void *z, pdu_t *pdu)
{
	if (pdu_send_and_free(pdu, z) != 0) {
		fprintf(stderr, "failed to send results to %s\n", OPTIONS.endpoint);
		return 3;
	}
	return 0;
}

int main(int argc, char **argv)
{
	OPTIONS.verbose = 0;
	OPTIONS.endpoint = strdup("tcp://127.0.0.1:2999");

	struct option long_opts[] = {
		{ "help",           no_argument, NULL, 'h' },
		{ "version",        no_argument, NULL, 'V' },
		{ "verbose",        no_argument, NULL, 'v' },
		{ "endpoint", required_argument, NULL, 'e' },
		{ "type",     required_argument, NULL, 't' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:t:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			logger(LOG_DEBUG, "handling -h/-?/--help");
			printf("send_bolo v%s\n", BOLO_VERSION);
			printf("Usage: send_bolo [-?hVv] [-e tcp://host:port] [-t (state|counter|sample|key|event|stream)]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -t, --type           what mode to run in (defaults to stream)\n");
			printf("  -e, --endpoint       bolo listener endpoint to connect to\n");
			exit(0);

		case 'V':
			logger(LOG_DEBUG, "handling -V/--version");
			printf("send_bolo v%s\n"
			       "Copyright (C) 2015 James Hunt\n",
			       BOLO_VERSION);
			exit(0);

		case 'v':
			logger(LOG_DEBUG, "handling -v/--verbose");
			OPTIONS.verbose++;
			break;

		case 'e':
			logger(LOG_DEBUG, "handling -e/--endpoint; replacing '%s' with '%s'",
				OPTIONS.endpoint, optarg);
			free(OPTIONS.endpoint);
			OPTIONS.endpoint = strdup(optarg);
			break;

		case 't':
			logger(LOG_DEBUG, "handling -t/--type; setting to '%s'", optarg);
			if (strcasecmp(optarg, "state") == 0) {
				OPTIONS.type = TYPE_STATE;

			} else if (strcasecmp(optarg, "counter") == 0) {
				OPTIONS.type = TYPE_COUNTER;

			} else if (strcasecmp(optarg, "sample") == 0) {
				OPTIONS.type = TYPE_SAMPLE;

			} else if (strcasecmp(optarg, "stream") == 0) {
				OPTIONS.type = TYPE_STREAM;

			} else if (strcasecmp(optarg, "key") == 0) {
				OPTIONS.type = TYPE_KEY;

			} else if (strcasecmp(optarg, "event") == 0) {
				OPTIONS.type = TYPE_EVENT;

			} else if (strcasecmp(optarg, "rate") == 0) {
				OPTIONS.type = TYPE_RATE;

			} else {
				fprintf(stderr, "invalid type '%s'\n", optarg);
				exit(1);
			}
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	pdu_t *pdu = NULL;

	if (OPTIONS.type == TYPE_STATE) {
		pdu = parse_state_pdu(argc - optind, argv + optind, NULL);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t state name code message\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_COUNTER) {
		pdu = parse_counter_pdu(argc - optind, argv + optind, NULL);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t counter name [increment]\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_SAMPLE) {
		pdu = parse_sample_pdu(argc - optind, argv + optind, NULL);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t sample name value [value ...]\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_RATE) {
		pdu = parse_rate_pdu(argc - optind, argv + optind, NULL);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t rate name value\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_STREAM) {
		if (argc - optind != 0) {
			fprintf(stderr, "USAGE: %s -t stream < input.file\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_KEY) {
		pdu = parse_setkeys_pdu(argc - optind, argv + optind);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t key key1=value1 key2=value2 ...\n", argv[0]);
			return 1;
		}

	} else if (OPTIONS.type == TYPE_EVENT) {
		pdu = parse_event_pdu(argc - optind, argv + optind, NULL);
		if (!pdu) {
			fprintf(stderr, "USAGE: %s -t event name [extra description ...]\n", argv[0]);
			return 1;
		}

	} else {
		fprintf(stderr, "USAGE: %s -t (sample|counter|state|key|event) args\n", argv[0]);
		return 1;
	}

	if (DEBUG) fprintf(stderr, "+>> allocating 0MQ context\n");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		fprintf(stderr, "failed to initialize 0MQ context; aborting (results NOT submitted)\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> allocating 0MQ PUSH socket to talk to %s\n", OPTIONS.endpoint);
	void *z = zmq_socket(zmq, ZMQ_PUSH);
	if (!z) {
		fprintf(stderr, "failed to create a PUSH socket; aborting (results NOT submitted)\n");
		return 3;
	}
	if (DEBUG) fprintf(stderr, "+>> connecting to %s\n", OPTIONS.endpoint);
	if (vx_vzmq_connect(z, OPTIONS.endpoint) != 0) {
		fprintf(stderr, "failed to connect to %s; aborting (results NOT submitted)\n", OPTIONS.endpoint);
		return 3;
	}

	int rc;
	if (pdu) {
		rc = send_pdu(z, pdu);

	} else {
		char buf[8192];
		for (;;) {
			if (isatty(0)) fprintf(stderr, "> ");
			if (fgets(buf, 8192, stdin) == NULL)
				break;

			char *a = strrchr(buf, '\n');
			if (a) *a = '\0';

			pdu = stream_pdu(buf);
			if (pdu) {
				rc = send_pdu(z, pdu);
				if (rc) break;
			} else {
				if (isatty(0))
					fprintf(stderr, "bad command '%s'\n", buf);
			}
		}
	}

	zmq_close(z);
	zmq_ctx_destroy(zmq);

	if (DEBUG) fprintf(stderr, "+>> completed successfully.\n");
	return rc;
}
