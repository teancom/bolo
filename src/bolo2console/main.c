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

#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <pcre.h>
#if !HAVE_PCRE_FREE_STUDY
#define pcre_free_study pcre_free
#endif

#include <vigor.h>
#include <bolo.h>

#define MASK_STATE       0x01
#define MASK_TRANSITION  0x02
#define MASK_EVENT       0x04
#define MASK_RATE        0x08
#define MASK_COUNTER     0x10
#define MASK_SAMPLE      0x20

#define MASK_ALL_THE_THINGS 0x3f

static struct {
	char *endpoint;
	int   verbose;
	char *match;
	int   mask;

	/* not used directly by option-handling... */
	pcre        *re;
	pcre_extra  *re_extra;
} OPTIONS = { 0 };

static void s_print(pdu_t *p)
{
	char *filter;
	if (strcmp(pdu_type(p), "TRANSITION") == 0
	 || strcmp(pdu_type(p), "STATE") == 0)
		filter = pdu_string(p, 1);
	else
		filter = pdu_string(p, 2);

	logger(LOG_INFO, "checking '%s' against /%s/", filter, OPTIONS.match);
	if (pcre_exec(OPTIONS.re, OPTIONS.re_extra, filter, strlen(filter), 0, 0, NULL, 0) == 0) {
		fprintf(stdout, "%s", pdu_type(p));
		int i;
		char *s;
		for (i = 1; i < pdu_size(p); i++) {
			fprintf(stdout, " %s", s = pdu_string(p, i));
			free(s);
		}
		fprintf(stdout, "\n");
	}

	free(filter);
}

int main(int argc, char **argv)
{
	OPTIONS.verbose   = 0;
	OPTIONS.endpoint  = strdup("tcp://127.0.0.1:2997");
	OPTIONS.match     = strdup(".");
	OPTIONS.mask      = 0;

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "verbose",          no_argument, NULL, 'v' },
		{ "version",          no_argument, NULL, 'V' },
		{ "endpoint",   required_argument, NULL, 'e' },
		{ "match",      required_argument, NULL, 'm' },
		{ "transitions",      no_argument, NULL, 'T' },
		{ "rates",            no_argument, NULL, 'R' },
		{ "states",           no_argument, NULL, 'A' },
		{ "counters",         no_argument, NULL, 'C' },
		{ "events",           no_argument, NULL, 'E' },
		{ "samples",          no_argument, NULL, 'S' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:m:TRACES", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf("bolo2console v%s\n", BOLO_VERSION);
			printf("Usage: bolo2console [-h?Vv] [-e tcp://host:port] [-TRACES] [-m PATTERN]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -T, --transitions    show TRANSITION data\n");
			printf("  -R, --rates          show RATE data\n");
			printf("  -A, --states         show STATE data\n");
			printf("  -C, --counters       show COUNTER data\n");
			printf("  -E, --events         show EVENT data\n");
			printf("  -S, --samples        show SAMPLE data\n");
			printf("  -m, --match          only display things matching a PCRE pattern\n");
			exit(0);

		case 'V':
			printf("bolo2console v%s\n"
			       "Copyright (c) 2016 The Bolo Authors.  All Rights Reserved.\n",
			       BOLO_VERSION);
			exit(0);

		case 'v':
			OPTIONS.verbose++;
			break;

		case 'e':
			free(OPTIONS.endpoint);
			OPTIONS.endpoint = strdup(optarg);
			break;

		case 'm':
			free(OPTIONS.match);
			OPTIONS.match = strdup(optarg);
			break;

		case 'T': OPTIONS.mask |= MASK_TRANSITION; break;
		case 'R': OPTIONS.mask |= MASK_RATE;       break;
		case 'A': OPTIONS.mask |= MASK_STATE;      break;
		case 'C': OPTIONS.mask |= MASK_COUNTER;    break;
		case 'E': OPTIONS.mask |= MASK_EVENT;      break;
		case 'S': OPTIONS.mask |= MASK_SAMPLE;     break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (!OPTIONS.mask)
		OPTIONS.mask = MASK_ALL_THE_THINGS;

	log_open("bolo2console", "console");
	if (OPTIONS.verbose)
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	else
		log_level(LOG_EMERG, NULL);

	logger(LOG_NOTICE, "starting up");

	const char *re_err;
	int re_off;
	OPTIONS.re = pcre_compile(OPTIONS.match, 0, &re_err, &re_off, NULL);
	if (!OPTIONS.re) {
		fprintf(stderr, "Bad --match pattern (%s): %s\n", OPTIONS.match, re_err);
		exit(1);
	}
	OPTIONS.re_extra = pcre_study(OPTIONS.re, 0, &re_err);

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
	if (vzmq_connect(z, OPTIONS.endpoint) != 0) {
		logger(LOG_ERR, "failed to connect to %s", OPTIONS.endpoint);
		return 3;
	}

	pdu_t *p;
	logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);

	signal_handlers();
	while (!signalled()) {
		while ((p = pdu_recv(z))) {
			logger(LOG_INFO, "received a [%s] PDU of %i frames", pdu_type(p), pdu_size(p));

			#define MATCH(x) (OPTIONS.mask & MASK_ ## x && strcmp(pdu_type(p), #x) == 0)
			if (MATCH(STATE)
			 || MATCH(TRANSITION)
			 || MATCH(EVENT)
			 || MATCH(RATE)
			 || MATCH(COUNTER)
			 || MATCH(SAMPLE))
				s_print(p);

			pdu_free(p);

			logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);
		}
	}

	logger(LOG_INFO, "shutting down");
	vzmq_shutdown(z, 0);
	zmq_ctx_destroy(zmq);
	return 0;
}
