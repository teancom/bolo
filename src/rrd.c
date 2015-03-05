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

#define RRD_MAP_FLUSH_INTERVAL 5

static struct {
	char *endpoint;
	char *root;
	char *hashfile;
	char *hashtmp;

	int   verbose;
	int   daemonize;

	char *pidfile;
	char *user;
	char *group;
} OPTIONS = { 0 };

static char *RRAs;

static char* s_rradefs(const char *filename)
{
	char *s;
	strings_t *rra = strings_new(NULL);
	FILE *io = fopen(filename, "r");
	if (!io)
		return strdup(" RRA:AVERAGE:0.5:1:14400"
		              " RRA:MIN:0.5:1:14400"
		              " RRA:MAX:0.5:1:14400"
		              " RRA:AVERAGE:0.5:60:24"
		              " RRA:MIN:0.5:60:24"
		              " RRA:MAX:0.5:60:24");

	char buf[8192];
	while (fgets(buf, 8192, io) != NULL) {
		char *nl = strrchr(buf, '\n');
		if (nl) *nl = '\0';
		strings_add(rra, buf);
	}
	s = strings_join(rra, " ");

	fclose(io);
	strings_free(rra);

	return s;
}

static int s_counter_create(const char *filename)
{
	char *s = string("bolo-rrdcreate %s --start n-1yr --step 60"
		" DS:x:GAUGE:120:U:U %s",
		filename, RRAs);
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
		" DS:var:GAUGE:120:U:U",
		filename, RRAs);
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

char* s_rrd_filename(const char *name, hash_t *map)
{
	if (!OPTIONS.hashfile)
		return strdup(name);

	sha1_t sha1;
	sha1_data(&sha1, name, strlen(name));
	char *file = string("%c%c/%c%c/%s",
		sha1.hex[0], sha1.hex[1],
		sha1.hex[2], sha1.hex[3], sha1.hex);

	char *dir;
	dir = string("%s/%c%c", OPTIONS.root,
			sha1.hex[0], sha1.hex[1]);
	mkdir(dir, 0777); free(dir);
	dir = string("%s/%c%c/%c%c", OPTIONS.root,
			sha1.hex[0], sha1.hex[1],
			sha1.hex[2], sha1.hex[3]);
	mkdir(dir, 0777); free(dir);

	if (!hash_get(map, file))
		hash_set(map, file, strdup(name));
	return file;
}

char* s_tmpname(const char *orig)
{
	char *tmp = string("%s.", orig);
	char *a = strrchr(tmp, '/');
	if (!a) return NULL;

	a++;
	memmove(a, a + 1, tmp + strlen(tmp) - a);
	*a = '.';
	return a;
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
		{ "hash",       required_argument, NULL, 'H' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+e:r:Fp:u:g:H:", long_opts, &idx);
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

		case 'H':
			free(OPTIONS.hashfile);
			OPTIONS.hashfile = strdup(optarg);

			free(OPTIONS.hashtmp);
			OPTIONS.hashtmp = s_tmpname(OPTIONS.hashfile);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	RRAs = s_rradefs("/usr/lib/bolo/bolo2rrd/rra.def");

	if (OPTIONS.daemonize) {
		log_open("bolo2rrd", "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			return 3;
		}
		umask(um);
	} else {
		log_open("bolo2rrd", "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");

	logger(LOG_DEBUG, "allocating 0MQ context\n");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context\n");
		return 3;
	}
	logger(LOG_DEBUG, "allocating 0MQ SUB socket to talk to %s\n", OPTIONS.endpoint);
	void *z = zmq_socket(zmq, ZMQ_SUB);
	if (!z) {
		logger(LOG_ERR, "failed to create a SUB socket\n");
		return 3;
	}
	logger(LOG_DEBUG, "setting subscriber filter\n");
	if (zmq_setsockopt(z, ZMQ_SUBSCRIBE, "", 0) != 0) {
		logger(LOG_ERR, "failed to set subscriber filter\n");
		return 3;
	}
	logger(LOG_DEBUG, "connecting to %s\n", OPTIONS.endpoint);
	if (vx_vzmq_connect(z, OPTIONS.endpoint) != 0) {
		logger(LOG_ERR, "failed to connect to %s\n", OPTIONS.endpoint);
		return 3;
	}

	int32_t flush_at = time_s() + 15;
	hash_t fmap;
	memset(&fmap, 0, sizeof(fmap));

	pdu_t *p;
	logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);
	while ((p = pdu_recv(z))) {
		logger(LOG_INFO, "received a [%s] PDU", pdu_type(p));

		if (strcmp(pdu_type(p), "COUNTER") == 0 && pdu_size(p) == 4) {
			char *ts    = pdu_string(p, 1);
			char *name  = pdu_string(p, 2);
			char *value = pdu_string(p, 3);

			char *filename = s_rrd_filename(name, &fmap);
			char *file = string("%s/%s.rrd", OPTIONS.root, filename);
			free(filename);

			struct stat st;
			if (stat(file, &st) != 0 && errno == ENOENT) {
				if (s_counter_create(file) != 0) {
					logger(LOG_ERR, "failed to create %s: %s\n", file, rrd_get_error());
					continue;
				}
			}

			if (s_counter_update(file, ts, value) != 0) {
				logger(LOG_ERR, "failed to update %s: %s\n", file, rrd_get_error());
				continue;
			}

			logger(LOG_INFO, "updated %s @%s v=%s\n", name, ts, value);
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

			char *filename = s_rrd_filename(name, &fmap);
			char *file = string("%s/%s.rrd", OPTIONS.root, filename);
			free(filename);

			struct stat st;
			if (stat(file, &st) != 0 && errno == ENOENT) {
				if (s_sample_create(file) != 0) {
					logger(LOG_ERR, "failed to create %s: %s\n", file, rrd_get_error());
					continue;
				}
			}

			if (s_sample_update(file, ts, n, min, max, sum, mean, var) != 0) {
				logger(LOG_ERR, "failed to update %s: %s\n", file, rrd_get_error());
				continue;
			}

			logger(LOG_INFO, "updated %s @%s n=%s, min=%s, max=%s, sum=%s, mean=%s, var=%s\n",
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

		if (OPTIONS.hashfile && time_s() >= flush_at) {
			logger(LOG_INFO, "flushing file map to %s", OPTIONS.hashfile);
			FILE *io = fopen(OPTIONS.hashtmp, "w");
			if (!io) {
				logger(LOG_ERR, "failed to open file map tempfile '%s' for writing: (%i) %s",
					OPTIONS.hashtmp, errno, strerror(errno));

			} else {
				char *k, *v;
				for_each_key_value(&fmap, k, v)
					fprintf(io, "%s %s\n", k, v);
				fclose(io);
				rename(OPTIONS.hashtmp, OPTIONS.hashfile);
			}

			flush_at = time_s() + RRD_MAP_FLUSH_INTERVAL;
		}

		logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);
	}

	logger(LOG_INFO, "shutting down");
	return 0;
}
