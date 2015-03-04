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
#include <rrd.h>

static struct {
	char *endpoint;
	int   verbose;
	int   daemonize;
	char *root;

	char *pidfile;
	char *user;
	char *group;
} OPTIONS = { 0 };

#define DEBUG OPTIONS.verbose > 0

static int s_counter_create(const char *filename)
{
	char *s = string("bolo-rrdcreate %s --start n-1yr --step 60"
		" DS:x:GAUGE:120:U:U"
		" RRA:LAST:0:1:14400"
		" RRA:AVERAGE:0.5:60:24"
		" RRA:MIN:0.5:60:24"
		" RRA:MAX:0.5:60:24",
		filename);
	strings_t *alist = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
	free(s);

	rrd_clear_error();
	int rc = rrd_create(alist->num, alist->strings);
	strings_free(alist);

	return rc;
}

static int s_counter_update(const char *filename, const char *ts, const char *v)
{
	char *s = string("bolo-rrdupdate %s -t x %s:%s", filename, ts, v);
	strings_t *alist = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
	free(s);

	rrd_clear_error();
	int rc = rrd_update(alist->num, alist->strings);
	strings_free(alist);

	return rc;
}

static int s_sample_create(const char *filename)
{
	char *s = string("bolo-rrdcreate %s --start n-1yr --step 60"
		" DS:n:GAUGE:120:U:U"
		" DS:min:GAUGE:120:U:U"
		" DS:max:GAUGE:120:U:U"
		" DS:sum:GAUGE:120:U:U"
		" DS:mean:GAUGE:120:U:U"
		" DS:var:GAUGE:120:U:U"
		" RRA:LAST:0:1:14400"
		" RRA:AVERAGE:0.5:60:24"
		" RRA:MIN:0.5:60:24"
		" RRA:MAX:0.5:60:24",
		filename);
	strings_t *alist = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
	free(s);

	rrd_clear_error();
	int rc = rrd_create(alist->num, alist->strings);
	strings_free(alist);

	return rc;
}

static int s_sample_update(const char *filename, const char *ts, const char *n, const char *min, const char *max, const char *sum, const char *mean, const char *var)
{
	char *s = string("bolo-rrdupdate %s -t n:min:max:sum:mean:var %s:%s:%s:%s:%s:%s:%s",
		filename, ts, n, min, max, sum, mean, var);

	strings_t *alist = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
	free(s);

	rrd_clear_error();
	int rc = rrd_update(alist->num, alist->strings);
	strings_free(alist);

	return rc;
}

int main(int argc, char **argv)
{
	OPTIONS.verbose   = 0;
	OPTIONS.endpoint  = strdup("tcp://127.0.0.1:2997");
	OPTIONS.root      = strdup("/var/lib/bolo/rrd");
	OPTIONS.daemonize = 1;
	OPTIONS.pidfile   = strdup("/var/run/bolo2rrd.pid");
	OPTIONS.user      = strdup("root");
	OPTIONS.group     = strdup("root");

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "verbose",          no_argument, NULL, 'v' },
		{ "endpoint",   required_argument, NULL, 'e' },
		{ "root",       required_argument, NULL, 'r' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "pidfile",    required_argument, NULL, 'p' },
		{ "user",       required_argument, NULL, 'u' },
		{ "group",      required_argument, NULL, 'g' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+e:r:Fp:u:g:", long_opts, &idx);
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

		case 'r':
			free(OPTIONS.root);
			OPTIONS.root = strdup(optarg);
			break;

		case 'F':
			OPTIONS.daemonize = 0;
			break;

		case 'p':
			free(OPTIONS.pidfile);
			OPTIONS.pidfile = strdup(optarg);
			break;

		case 'u':
			free(OPTIONS.user);
			OPTIONS.user = strdup(optarg);
			break;

		case 'g':
			free(OPTIONS.group);
			OPTIONS.group = strdup(optarg);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}


	if (OPTIONS.daemonize) {
		log_open("bolo2rrd", "daemon");
		logger(LOG_NOTICE, "starting up");

		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			return 3;
		}
	} else {
		log_open("bolo2rrd", "console");
	}

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

	pdu_t *p;
	logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);
	while ((p = pdu_recv(z))) {
		logger(LOG_INFO, "received a [%s] PDU", pdu_type(p));

		if (strcmp(pdu_type(p), "COUNTER") == 0 && pdu_size(p) == 4) {
			char *ts    = pdu_string(p, 1);
			char *name  = pdu_string(p, 2);
			char *value = pdu_string(p, 3);

			char *file = string("%s/%s.rrd", OPTIONS.root, name);
			struct stat st;
			if (stat(file, &st) != 0 && errno == ENOENT) {
				if (s_counter_create(file) != 0) {
					fprintf(stderr, "failed to create %s: %s\n", file, rrd_get_error());
					continue;
				}
			}

			if (s_counter_update(file, ts, value) != 0) {
				fprintf(stderr, "failed to update %s: %s\n", file, rrd_get_error());
				continue;
			}

			printf("updated %s @%s v=%s\n", name, ts, value);
			free(ts);
			free(name);
			free(value);
			free(file);

		} else if (strcmp(pdu_type(p), "SAMPLE") == 0 && pdu_size(p) == 9) {
			char *ts    = pdu_string(p, 1);
			char *name  = pdu_string(p, 2);
			char *n     = pdu_string(p, 3);
			char *min   = pdu_string(p, 4);
			char *max   = pdu_string(p, 5);
			char *sum   = pdu_string(p, 6);
			char *mean  = pdu_string(p, 7);
			char *var   = pdu_string(p, 8);

			char *file = string("/tmp/%s.rrd", name);
			struct stat st;
			if (stat(file, &st) != 0 && errno == ENOENT) {
				if (s_sample_create(file) != 0) {
					fprintf(stderr, "failed to create %s: %s\n", file, rrd_get_error());
					continue;
				}
			}

			if (s_sample_update(file, ts, n, min, max, sum, mean, var) != 0) {
				fprintf(stderr, "failed to update %s: %s\n", file, rrd_get_error());
				continue;
			}

			printf("updated %s @%s n=%s, min=%s, max=%s, sum=%s, mean=%s, var=%s\n",
				name, ts, n, min, max, sum, mean, var);
			free(ts);
			free(name);
			free(n);
			free(min);
			free(max);
			free(sum);
			free(mean);
			free(var);
			free(file);
		}

		pdu_free(p);
		logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);
	}

	logger(LOG_INFO, "shutting down");
	return 0;
}
