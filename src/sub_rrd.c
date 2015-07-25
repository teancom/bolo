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
#include <pthread.h>
#include <getopt.h>
#include <rrd.h>

#define RRD_MAP_FLUSH_INTERVAL 5
#define CREATOR_BUS "inproc://rrd.creator"
#define UPDATER_BUS "inproc://rrd.updater"

static struct {
	char *endpoint;
	char *root;
	char *hashfile;
	char *hashtmp;

	int   verbose;
	int   daemonize;
	char *rrdcached;

	char *pidfile;
	char *user;
	char *group;

	int   updaters;
	int   creators;

	void *submit;
	char *prefix;
	float coverage;
} OPTIONS = { 0 };

static char *RRAs;

static char* s_rradefs(const char *filename)
{
	char *s;
	FILE *io = fopen(filename, "r");
	if (!io)
		return strdup("RRA:AVERAGE:0.5:1:14400"
		             " RRA:MIN:0.5:1:14400"
		             " RRA:MAX:0.5:1:14400"
		             " RRA:AVERAGE:0.5:60:24"
		             " RRA:MIN:0.5:60:24"
		             " RRA:MAX:0.5:60:24");

	strings_t *rra = strings_new(NULL);
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
	char *s = string("DS:x:GAUGE:120:U:U %s", RRAs);
	strings_t *alist = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
	free(s);

	rrd_clear_error();
	int rc = rrd_create_r(
		filename,
		60,
		time_s() - 365 * 86400,
		alist->num,
		(const char **)alist->strings);
	strings_free(alist);

	return rc;
}

static int s_counter_update(const char *filename, const char *ts, const char *v)
{
	char *argv[2] = {
		string("%s:%s", ts, v),
		NULL,
	};

	rrd_clear_error();
	int rc = rrd_update_r(
		filename,
		NULL,
		1,
		(const char **)argv);

	free(argv[0]);
	return rc;
}

static int s_sample_create(const char *filename)
{
	char *s = string(""
		 "DS:n:GAUGE:120:U:U"
		" DS:min:GAUGE:120:U:U"
		" DS:max:GAUGE:120:U:U"
		" DS:sum:GAUGE:120:U:U"
		" DS:mean:GAUGE:120:U:U"
		" DS:var:GAUGE:120:U:U %s", RRAs);
	strings_t *alist = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
	free(s);

	rrd_clear_error();
	int rc = rrd_create_r(
		filename,
		60,
		time_s() - 365 * 86400,
		alist->num,
		(const char **)alist->strings);
	strings_free(alist);

	return rc;
}

static int s_sample_update(const char *filename, const char *ts, const char *n, const char *min, const char *max, const char *sum, const char *mean, const char *var)
{
	char *argv[2] = {
		string("%s:%s:%s:%s:%s:%s:%s", ts, n, min, max, sum, mean, var),
		NULL,
	};

	rrd_clear_error();
	int rc = rrd_update_r(
		filename,
		NULL,
		1,
		(const char **)argv);

	free(argv[0]);
	return rc;
}

static int s_rate_create(const char *filename)
{
	char *s = string("DS:value:GAUGE:120:U:U %s", RRAs);
	strings_t *alist = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
	free(s);

	rrd_clear_error();
	int rc = rrd_create_r(
		filename,
		60,
		time_s() - 365 * 86400,
		alist->num,
		(const char **)alist->strings);
	strings_free(alist);

	return rc;
}

static int s_rate_update(const char *filename, const char *ts, const char *v)
{
	char *argv[2] = {
		string("%s:%s", ts, v),
		NULL,
	};

	rrd_clear_error();
	int rc = rrd_update_r(
		filename,
		NULL,
		1,
		(const char **)argv);

	free(argv[0]);
	return rc;
}

static char* s_rrd_filename(const char *name, hash_t *map)
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

static char* s_tmpname(const char *orig)
{
	char *tmp = string(".%s", orig);
	char *slash = strrchr(tmp, '/');
	if (!slash)
		return tmp;

	/* move that dot! */
	char *a = tmp;
	while (a != slash) { char c = *a; *a = *(a + 1); *++a = c; }
	return tmp;
}

static int s_read_map(hash_t *map)
{
	if (!OPTIONS.hashfile) return 0;

	FILE *io = fopen(OPTIONS.hashfile, "r");
	if (!io) return 0;

	char buf[8192];
	while (fgets(buf, 8192, io) != NULL) {
		char *x;
		x = strrchr(buf, '\n');
		if (x) *x = '\0';

		x = strchr(buf, ' ');
		if (!x) continue;

		*x++ = '\0';
		hash_set(map, buf, strdup(x));
	}

	fclose(io);
	return 0;
}

static int s_write_map(hash_t *map)
{
	FILE *io = fopen(OPTIONS.hashtmp, "w");
	if (!io)
		return 1;

	char *k, *v;
	for_each_key_value(map, k, v)
		fprintf(io, "%s %s\n", k, v);
	fclose(io);
	rename(OPTIONS.hashtmp, OPTIONS.hashfile);
	return 0;
}

void* creator_thread(void *zmq)
{
	/* set up some thread-specific stuff in -lrrd_th */
	rrd_get_context();

	int *rc = vmalloc(sizeof(int));
	*rc = 0;

	void *pull = zmq_socket(zmq, ZMQ_PULL);
	if (!pull) {
		logger(LOG_ERR, "failed to create a PULL socket in the creator thread");
		*rc = 1;
		return rc;
	}

	logger(LOG_DEBUG, "creator: connecting to bus %s", CREATOR_BUS);
	*rc = zmq_connect(pull, CREATOR_BUS);
	if (*rc != 0) {
		logger(LOG_ERR, "creator: unable to connect to bus %s", CREATOR_BUS);
		return rc;
	}

	logger(LOG_INFO, "creator: listening for inbound PDUs");

	stopwatch_t watch;
	uint64_t spent = 0;
	pdu_t *p;
	while ((p = pdu_recv(pull))) {
		STOPWATCH(&watch, spent) {
			logger(LOG_DEBUG, "creator: received a [%s] PDU", pdu_type(p));

			char *file  = pdu_string(p, 1);
			char *name  = pdu_string(p, 3);

			struct stat st;
			if (stat(file, &st) != 0) {
				if (errno == ENOENT) {
					if (strcmp(pdu_type(p), "COUNTER") == 0) {
						logger(LOG_INFO, "creator: provisioning new RRD file %s for COUNTER metric %s", file, name);
						if (s_counter_create(file) != 0)
							logger(LOG_ERR, "creator: failed to create %s: %s", file, rrd_get_error());

					} else if (strcmp(pdu_type(p), "SAMPLE") == 0) {
						logger(LOG_INFO, "creator: provisioning new RRD file %s for SAMPLE metric %s", file, name);
						if (s_sample_create(file) != 0)
							logger(LOG_ERR, "creator: failed to create %s: %s", file, rrd_get_error());

					} else if (strcmp(pdu_type(p), "RATE") == 0) {
						logger(LOG_INFO, "creator: provisioning new RRD file %s for RATE metric %s", file, name);
						if (s_rate_create(file) != 0)
							logger(LOG_ERR, "creator: failed to create %s: %s", file, rrd_get_error());

					} else {
						logger(LOG_ERR, "creator: unknown sample type %s; skipping", pdu_type(p));
					}

				} else {
					logger(LOG_ERR, "creator: stat(%s) failed: %s", file, strerror(errno));
				}

			} else {
				logger(LOG_INFO, "creator: %s already exists; skipping", file);
			}

			free(file);
			free(name);
		}

		if (OPTIONS.submit && probable(OPTIONS.coverage)) {
			char *name = string("%s:sys:bolo2rrd:create.time.s", OPTIONS.prefix);
			pdu_send_and_free(sample_pdu(name, 1, spent / 1000.), OPTIONS.submit);
			free(name);
		}
	}

	logger(LOG_DEBUG, "creator thread shutting down");
	zmq_close(pull);
	return rc;
}

void* updater_thread(void *zmq)
{
	/* set up some thread-specific stuff in -lrrd_th */
	rrd_get_context();

	int *rc = vmalloc(sizeof(int));
	*rc = 0;

	void *pull = zmq_socket(zmq, ZMQ_PULL);
	if (!pull) {
		logger(LOG_ERR, "failed to create a PULL socket in updater thread");
		*rc = 1;
		return rc;
	}

	logger(LOG_DEBUG, "updater: connecting to bus %s", UPDATER_BUS);
	*rc = zmq_connect(pull, UPDATER_BUS);
	if (*rc != 0) {
		logger(LOG_ERR, "updater: unable to connect to bus %s", UPDATER_BUS);
		return rc;
	}

	logger(LOG_INFO, "updater: listening for inbound PDUs");

	stopwatch_t watch;
	uint64_t spent = 0;
	pdu_t *p;
	while ((p = pdu_recv(pull))) {
		STOPWATCH(&watch, spent) {
			logger(LOG_DEBUG, "updater: received a [%s] PDU", pdu_type(p));

			char *file  = pdu_string(p, 1);
			char *ts    = pdu_string(p, 2);
			char *name  = pdu_string(p, 3);
			logger(LOG_DEBUG, "updater: looking for metric %s in file %s", name, file);

			struct stat st;
			if (stat(file, &st) == 0) {
				if (strcmp(pdu_type(p), "COUNTER") == 0 && pdu_size(p) == 1+4) {
					char *value = pdu_string(p, 4);

					if (s_counter_update(file, ts, value) == 0)
						logger(LOG_INFO, "updater: recorded %s @%s v=%s", name, ts, value);
					else
						logger(LOG_ERR, "updater: failed to update %s: %s", file, rrd_get_error());

					free(value);

				} else if (strcmp(pdu_type(p), "SAMPLE") == 0 && pdu_size(p) == 1+9) {
					char *n     = pdu_string(p, 4);
					char *min   = pdu_string(p, 5);
					char *max   = pdu_string(p, 6);
					char *sum   = pdu_string(p, 7);
					char *mean  = pdu_string(p, 8);
					char *var   = pdu_string(p, 9);

					if (s_sample_update(file, ts, n, min, max, sum, mean, var) != 0)
						logger(LOG_ERR, "updater: failed to update %s: %s", file, rrd_get_error());
					else
						logger(LOG_INFO, "updater: recorded %s @%s n=%s, min=%s, max=%s, sum=%s, mean=%s, var=%s",
						name, ts, n, min, max, sum, mean, var);

					free(n);
					free(min);
					free(max);
					free(sum);
					free(mean);
					free(var);

				} else if (strcmp(pdu_type(p), "RATE") == 0 && pdu_size(p) == 1+5) {
					char *v = pdu_string(p, 4);

					if (s_rate_update(file, ts, v) != 0)
						logger(LOG_ERR, "updater: failed to update %s: %s", file, rrd_get_error());
					else
						logger(LOG_INFO, "updater: recorded %s @%s v=%s", name, ts, v);

					free(v);
				}

				free(file);
				free(ts);
				free(name);
			}
		}

		if (OPTIONS.submit && probable(OPTIONS.coverage)) {
			char *name = string("%s:sys:bolo2rrd:update.time.s", OPTIONS.prefix);
			pdu_send_and_free(sample_pdu(name, 1, spent / 1000.), OPTIONS.submit);
			free(name);
		}
	}

	logger(LOG_DEBUG, "updater thread shutting down");
	zmq_close(pull);
	return rc;
}

int main(int argc, char **argv)
{
	OPTIONS.verbose   = 0;
	OPTIONS.endpoint  = strdup("tcp://127.0.0.1:2997");
	OPTIONS.root      = strdup("/var/lib/bolo/rrd");
	OPTIONS.daemonize = 1;
	OPTIONS.rrdcached = NULL;
	OPTIONS.pidfile   = strdup("/var/run/bolo2rrd.pid");
	OPTIONS.user      = strdup("root");
	OPTIONS.group     = strdup("root");
	OPTIONS.creators  = 2;
	OPTIONS.updaters  = 8;
	OPTIONS.submit    = NULL;
	OPTIONS.prefix    = NULL; /* will be set later, if needed */
	OPTIONS.coverage  = 0.1;

	char *submit_endpoint = NULL;

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
		{ "rrdcached",  required_argument, NULL, 'C' },
		{ "updaters",   required_argument, NULL, 'U' },
		{ "creators",   required_argument, NULL, 'c' },
		{ "submit",     required_argument, NULL, 'S' },
		{ "prefix",     required_argument, NULL, 'P' },
		{ "coverage",   required_argument, NULL,  1  },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+e:r:Fp:u:g:H:C:c:U:S:P:", long_opts, &idx);
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

		case 'C':
			free(OPTIONS.rrdcached);
			OPTIONS.rrdcached = strdup(optarg);
			break;

		case 'U':
			OPTIONS.updaters = strtoll(optarg, NULL, 10);
			break;

		case 'c':
			OPTIONS.creators = strtoll(optarg, NULL, 10);
			break;

		case 'S':
			free(submit_endpoint);
			submit_endpoint = strdup(optarg);
			break;

		case 'P':
			free(OPTIONS.prefix);
			OPTIONS.prefix = strdup(optarg);
			break;

		case 1:
			OPTIONS.coverage = strtof(optarg, NULL); /* FIXME: error/bounds checking */
			OPTIONS.coverage /= 100.0;
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (!OPTIONS.prefix)
		OPTIONS.prefix = fqdn();

	struct stat st;
	if (stat(OPTIONS.root, &st) != 0) {
		fprintf(stderr, "%s: %s\n", OPTIONS.root, strerror(errno));
		return 3;
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

	if (OPTIONS.rrdcached)
		setenv("RRDCACHED_ADDRESS", OPTIONS.rrdcached, 1);

	logger(LOG_DEBUG, "allocating 0MQ context");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		return 3;
	}
	logger(LOG_DEBUG, "allocating 0MQ SUB socket to talk to %s", OPTIONS.endpoint);
	void *sub = zmq_socket(zmq, ZMQ_SUB);
	if (!sub) {
		logger(LOG_ERR, "failed to create a SUB socket");
		return 3;
	}
	logger(LOG_DEBUG, "setting subscriber filter");
	if (zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "", 0) != 0) {
		logger(LOG_ERR, "failed to set subscriber filter");
		return 3;
	}
	logger(LOG_DEBUG, "connecting to %s", OPTIONS.endpoint);
	if (vx_vzmq_connect(sub, OPTIONS.endpoint) != 0) {
		logger(LOG_ERR, "failed to connect to %s", OPTIONS.endpoint);
		return 3;
	}

	logger(LOG_DEBUG, "connecting to %s (for metric submission)", submit_endpoint);
	if (submit_endpoint) {
		OPTIONS.submit = zmq_socket(zmq, ZMQ_PUSH);
		if (!OPTIONS.submit) {
			logger(LOG_ERR, "failed to create a PUSH socket");
		} else if (vx_vzmq_connect(OPTIONS.submit, submit_endpoint) != 0) {
			logger(LOG_ERR, "failed to connect to %s", submit_endpoint);
		}
	}

	void *creator_bus = zmq_socket(zmq, ZMQ_PUSH);
	if (!creator_bus) {
		logger(LOG_ERR, "failed to create a PUSH socket for the creator thread bus");
		return 3;
	}
	logger(LOG_DEBUG, "binding internal creator thread bus router to %s", CREATOR_BUS);
	int rc = zmq_bind(creator_bus, CREATOR_BUS);
	if (rc != 0) {
		logger(LOG_ERR, "failed to bind PUSH socket to %s", CREATOR_BUS);
		return 3;
	}

	void *updater_bus = zmq_socket(zmq, ZMQ_PUSH);
	if (!updater_bus) {
		logger(LOG_ERR, "failed to create a PUSH socket for the updater thread bus");
		return 3;
	}
	logger(LOG_DEBUG, "binding internal updater thread bus router to %s" UPDATER_BUS);
	rc = zmq_bind(updater_bus, UPDATER_BUS);
	if (rc != 0) {
		logger(LOG_ERR, "failed to bind PUSH socket to %s", UPDATER_BUS);
		return 3;
	}

	int32_t flush_at = time_s() + RRD_MAP_FLUSH_INTERVAL;
	hash_t fmap;
	memset(&fmap, 0, sizeof(fmap));
	if (s_read_map(&fmap) != 0)
		logger(LOG_ERR, "failed to read file map '%s': (%u) %s",
			OPTIONS.hashfile, errno, strerror(errno));

	/* install signal handlers before we spin off threads */
	signal_handlers();

	pthread_t tid;
	logger(LOG_INFO, "spinning up %lu creator threads", OPTIONS.creators);
	int i;
	for (i = 0; i < OPTIONS.updaters; i++) {
		rc = pthread_create(&tid, NULL, creator_thread, zmq);
		if (rc != 0) {
			logger(LOG_ERR, "failed to spin up creator thread #%i", i+1);
			return 3;
		}
	}

	logger(LOG_INFO, "spinning up %lu updater threads", OPTIONS.updaters);
	for (i = 0; i < OPTIONS.updaters; i++) {
		rc = pthread_create(&tid, NULL, updater_thread, zmq);
		if (rc != 0) {
			logger(LOG_ERR, "failed to spin up updater thread #%i", i+1);
			return 3;
		}
	}

	pdu_t *p;
	while (!signalled()) {
		while ((p = pdu_recv(sub))) {
			char *name = NULL;
			if ((strcmp(pdu_type(p), "COUNTER") == 0 && pdu_size(p) == 4)
			 || (strcmp(pdu_type(p), "SAMPLE")  == 0 && pdu_size(p) == 9)
			 || (strcmp(pdu_type(p), "RATE")    == 0 && pdu_size(p) == 5)) {
				logger(LOG_INFO, "received a [%s] PDU", pdu_type(p));
				name = pdu_string(p, 2);

			} else {
				continue;
			}

			char *filename = s_rrd_filename(name, &fmap);
			char *file = string("%s/%s.rrd", OPTIONS.root, filename);

			/* repack the PDU, with the filename as the first argument */
			pdu_t *relay = pdu_make(pdu_type(p), 1, file);
			for (i = 1; i < pdu_size(p); i++) {
				char *f = pdu_string(p, i);
				pdu_extend(relay, f, strlen(f));
				free(f);
			}

			struct stat st;
			if (stat(file, &st) != 0 && errno == ENOENT) {
				logger(LOG_DEBUG, "relaying [%s] PDU to creator thread", pdu_type(p));
				pdu_send_and_free(relay, creator_bus);

			} else {
				logger(LOG_DEBUG, "relaying [%s] PDU to an updater thread", pdu_type(p));
				pdu_send_and_free(relay, updater_bus);
			}

			free(filename);
			free(file);
			free(name);

			pdu_free(p);

			if (OPTIONS.hashfile && time_s() >= flush_at) {
				logger(LOG_INFO, "flushing file map to %s", OPTIONS.hashfile);
				if (s_write_map(&fmap) != 0)
					logger(LOG_ERR, "failed to open file map tempfile '%s' for writing: (%i) %s",
						OPTIONS.hashtmp, errno, strerror(errno));

				flush_at = time_s() + RRD_MAP_FLUSH_INTERVAL;
			}
		}
	}

	logger(LOG_INFO, "shutting down");
	vzmq_shutdown(sub,         50);
	vzmq_shutdown(creator_bus, 50);
	vzmq_shutdown(updater_bus, 50);
	if (OPTIONS.submit)
		vzmq_shutdown(OPTIONS.submit, 0);

	zmq_ctx_destroy(zmq);
	return 0;
}
