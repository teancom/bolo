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

#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <getopt.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <rrd.h>
#include <rrd_client.h>

#include <vigor.h>
#include <bolo.h>

/**************************/

static char *CACHED = NULL;

typedef struct {
	char *parents[2];      /* absolute paths to the two parent directories */
	char *abspath;         /* absolute path to the RRD file */
	char *relpath;         /* "aa/bb/aabbccdd..." (without the .rrd suffix) */
	char *name;            /* metric name */
} rrdfile_t;

void rrdfile_free(rrdfile_t *file);
rrdfile_t *rrdfile_parse(const char *root, const char *name, const char *sha1);
rrdfile_t *rrd_filename(const char *root, const char *metric);

typedef struct {
	hash_t  hash;
	char   *file;
	char   *tmpfile;
} rrdmap_t;

rrdmap_t* rrdmap_new(const char *db);
void rrdmap_free(rrdmap_t *map);
int rrdmap_read(rrdmap_t *map, const char *root);
int rrdmap_write(rrdmap_t *map);

rrdmap_t* rrdmap_new(const char *db) /* {{{ */
{
	assert(db != NULL);

	rrdmap_t *map = vmalloc(sizeof(rrdmap_t));
	map->file = strdup(db);

	char *t = string(".%s", db);
	char *slash = strrchr(t, '/');
	if (slash) {
		char *a = t; /* move that dot! */
		while (a != slash) { char c = *a; *a = *(a + 1); *++a = c; }
	}
	map->tmpfile = t;

	return map;
}
/* }}} */
void rrdmap_free(rrdmap_t *map) /* {{{ */
{
	if (!map)
		return;

	char *k;
	rrdfile_t *file;
	for_each_key_value(&map->hash, k, file)
		rrdfile_free(file);
	hash_done(&map->hash, 0);

	free(map->file);
	free(map->tmpfile);
	free(map);
}
/* }}} */
int rrdmap_read(rrdmap_t *map, const char *root) /* {{{ */
{
	assert(map != NULL);

	FILE *io = fopen(map->file, "r");
	if (!io) return 0;

	char buf[8192];
	while (fgets(buf, 8192, io) != NULL) {
		char *x;
		char *sha1, *name;

		x = strrchr(buf, '\n');
		if (x) *x = '\0';

		x = strchr(buf, ' ');
		if (!x) continue;

		*x++ = '\0';
		name = x;

		x = strrchr(buf, '/');
		if (!x) continue;
		sha1 = ++x;

		hash_set(&map->hash, name, rrdfile_parse(root, name, sha1));
	}

	fclose(io);
	return 0;
}
/* }}} */
int rrdmap_write(rrdmap_t *map) /* {{{ */
{
	FILE *io = fopen(map->tmpfile, "w");
	if (!io)
		return 1;

	char *_; rrdfile_t *file;
	for_each_key_value(&map->hash, _, file)
		fprintf(io, "%s %s\n", file->relpath, file->name);
	fclose(io);

	return rename(map->tmpfile, map->file);
}
/* }}} */
void rrdfile_free(rrdfile_t *file) /* {{{ */
{
	if (!file) return;
	free(file->parents[0]);
	free(file->parents[1]);
	free(file->relpath);
	free(file->abspath);
	free(file->name);
	free(file);
}
/* }}} */
rrdfile_t *rrdfile_parse(const char *root, const char *name, const char *sha1) /* {{{ */
{
	rrdfile_t *file = vmalloc(sizeof(rrdfile_t));
	file->name = strdup(name);

	file->parents[0] = string("%s/%c%c",      root, sha1[0], sha1[1]);

	file->parents[1] = string("%s/%c%c/%c%c", root, sha1[0], sha1[1],
	                                                sha1[2], sha1[3]);

	file->relpath    = string("%c%c/%c%c/%s",       sha1[0], sha1[1],
	                                                sha1[2], sha1[3], sha1);

	file->abspath    = string("%s/%s.rrd",    root, file->relpath);

	return file;
}
/* }}} */
rrdfile_t *rrd_filename(const char *root, const char *metric) /* {{{ */
{
	sha1_t sha1;
	sha1_data(&sha1, metric, strlen(metric));
	return rrdfile_parse(root, metric, sha1.hex);
}
/* }}} */

/**************************/

typedef struct {
	int id;           /* simple identifying number for the thread; used in logging */

	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */

	void *queue;      /* PULL: hooked up to dispatcher.creates; receives RRD create requests */

	reactor_t *reactor;

	char *rras;
} creator_t;

int creator_thread(void *zmq, int id, const char *rras);


typedef struct {
	int id;           /* simple identifying number for the thread; used in logging */

	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */

	void *queue;      /* PULL: hooked up to dispatcher.updates; receives RRD update requests */

	reactor_t *reactor;
} updater_t;

int updater_thread(void *zmq, int id);


typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */
	void *tock;       /* SUB:  hooked up to scheduler.tick, for timing interrupts */

	void *subscriber; /* SUB:  subscriber to bolo broadcast port (external) */

	void *updates;    /* PUSH: fair-queue of updater_t workers (for RRD update requests) */
	void *creates;    /* PUSH: fair-queue of creator_t workers (for RRD create requests) */

	reactor_t *reactor;

	rrdmap_t *map;
	char     *root;
} dispatcher_t;

int dispatcher_thread(void *zmq, const char *endpoint, const char *root, const char *mapfile);

static int _dispatcher_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	dispatcher_t *dispatcher = (dispatcher_t*)_;

	if (socket == dispatcher->control) {
		if (strcmp(pdu_type(pdu), "TERMINATE") == 0) {
			logger(LOG_DEBUG, "flushing RRD map to disk");
			int rc = rrdmap_write(dispatcher->map);
			if (rc != 0)
				logger(LOG_WARNING, "failed to write RRD map to disk");

			return VIGOR_REACTOR_HALT;
		}

		logger(LOG_ERR, "dispatcher thread received unrecognized [%s] PDU from control socket; ignoring",
			pdu_type(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == dispatcher->tock) {
		logger(LOG_DEBUG, "flushing RRD map to disk");
		int rc = rrdmap_write(dispatcher->map);
		if (rc != 0)
			logger(LOG_WARNING, "failed to write RRD map to disk");
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == dispatcher->subscriber) {
		stopwatch_t watch;
		uint64_t spent = 0;

		STOPWATCH(&watch, spent) {
			char *name = NULL;
			if ((strcmp(pdu_type(pdu), "COUNTER") == 0 && pdu_size(pdu) == 4)
			 || (strcmp(pdu_type(pdu), "SAMPLE")  == 0 && pdu_size(pdu) == 9)
			 || (strcmp(pdu_type(pdu), "RATE")    == 0 && pdu_size(pdu) == 5)) {
				//logger(LOG_INFO, "received a [%s] PDU", pdu_type(pdu));
				name = pdu_string(pdu, 2);

			} else {
				return VIGOR_REACTOR_CONTINUE;
			}

			rrdfile_t *file = hash_get(&dispatcher->map->hash, name);
			if (!file) {
				file = rrd_filename(dispatcher->root, name);
				hash_set(&dispatcher->map->hash, name, file);
			}
			free(name);

			struct stat st;
			pdu_t *relay;
			if (stat(file->abspath, &st) != 0) {
				relay = pdu_make("CREATE", 4,
					file->parents[0], file->parents[1], file->abspath, pdu_type(pdu));

				pdu_send_and_free(relay, dispatcher->creates);


			} else {
				char *s; s = pdu_string(pdu, 1);
				relay = pdu_make("UPDATE", 3, file->abspath, s, pdu_type(pdu));
				free(s);

				int i;
				for (i = 3; i < pdu_size(pdu); i++) {
					char *s = pdu_string(pdu, i);
					pdu_extendf(relay, "%s", s);
					free(s);
				}

				pdu_send_and_free(relay, dispatcher->updates);
			}
		}

		pdu_t *perf = pdu_make("SAMPLE", 1, "dispatch.time.s");
		pdu_extendf(perf, "%lf", spent / 1000.);
		pdu_send_and_free(perf, dispatcher->monitor);

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
static void * _dispatcher_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	dispatcher_t *dispatcher = (dispatcher_t*)_;

	reactor_go(dispatcher->reactor);

	logger(LOG_DEBUG, "dispatcher: shutting down");

	zmq_close(dispatcher->subscriber);
	zmq_close(dispatcher->updates);
	zmq_close(dispatcher->creates);
	zmq_close(dispatcher->monitor);
	zmq_close(dispatcher->tock);
	zmq_close(dispatcher->control);

	reactor_free(dispatcher->reactor);
	rrdmap_free(dispatcher->map);
	free(dispatcher->root);
	free(dispatcher);

	logger(LOG_DEBUG, "dispatcher: terminated");
	return NULL;
}
/* }}} */
int dispatcher_thread(void *zmq, const char *endpoint, const char *root, const char *mapfile) /* {{{ */
{
	assert(zmq != NULL);
	assert(endpoint != NULL);
	assert(mapfile != NULL);

	int rc;

	logger(LOG_INFO, "initializing dispatcher thread");

	dispatcher_t *dispatcher = vmalloc(sizeof(dispatcher_t));
	dispatcher->root = strdup(root);

	dispatcher->map = rrdmap_new(mapfile);
	rc = rrdmap_read(dispatcher->map, root);
	if (rc != 0)
		return rc;


	logger(LOG_DEBUG, "connecting dispatcher.control -> supervisor");
	rc = bolo_subscriber_connect_supervisor(zmq, &dispatcher->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting dispatcher.monitor -> monitor");
	rc = bolo_subscriber_connect_monitor(zmq, &dispatcher->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting dispatcher.tock -> scheduler");
	rc = bolo_subscriber_connect_scheduler(zmq, &dispatcher->tock);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting dispatcher.subscriber <- bolo at %s", endpoint);
	dispatcher->subscriber = zmq_socket(zmq, ZMQ_SUB);
	if (!dispatcher->subscriber)
		return -1;
	rc = zmq_setsockopt(dispatcher->subscriber, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;
	rc = vzmq_connect_af(dispatcher->subscriber, endpoint, AF_UNSPEC);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "binding PUSH socket dispatcher.updates");
	dispatcher->updates = zmq_socket(zmq, ZMQ_PUSH);
	if (!dispatcher->updates)
		return -1;
	rc = zmq_bind(dispatcher->updates, "inproc://bolo2rrd/v1/dispatcher.updates");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "binding PUSH socket dispatcher.creates");
	dispatcher->creates = zmq_socket(zmq, ZMQ_PUSH);
	if (!dispatcher->creates)
		return -1;
	rc = zmq_bind(dispatcher->creates, "inproc://bolo2rrd/v1/dispatcher.creates");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "setting up dispatcher event reactor");
	dispatcher->reactor = reactor_new();
	if (!dispatcher->reactor)
		return -1;

	logger(LOG_DEBUG, "dispatcher: registering dispatcher.control with event reactor");
	rc = reactor_set(dispatcher->reactor, dispatcher->control, _dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: registering dispatcher.tock with event reactor");
	rc = reactor_set(dispatcher->reactor, dispatcher->tock, _dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: registering dispatcher.subscriber with event reactor");
	rc = reactor_set(dispatcher->reactor, dispatcher->subscriber, _dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _dispatcher_thread, dispatcher);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */


static int _creator_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	creator_t *creator = (creator_t*)_;

	if (socket == creator->control) {
		if (strcmp(pdu_type(pdu), "TERMINATE") == 0)
			return VIGOR_REACTOR_HALT;
		logger(LOG_ERR, "creator[%i] thread received unrecognized [%s] PDU from control socket; ignoring",
			creator->id, pdu_type(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == creator->queue) {

		if (strcmp(pdu_type(pdu), "CREATE") != 0) {
			logger(LOG_DEBUG, "creator[%i] discarding unhandled [%s] PDU", creator->id, pdu_type(pdu));
			return VIGOR_REACTOR_CONTINUE;
		}

		/*
		       +---------------+
		       | CREATE        |
		       +---------------+
		       | /path         |
		       | /path/to      |
		       | /path/to/file |
		       | <TYPE>        |
		       +---------------+
		 */

		char *file = pdu_string(pdu, 3);
		struct stat st;
		if (stat(file, &st) == 0) {
			logger(LOG_DEBUG, "creator[%i] rrd %s already exists; logging create.misdirect", creator->id);
			pdu_send_and_free(pdu_make("COUNT", 1, "create.misdirects"), creator->monitor);
			free(file);
			return VIGOR_REACTOR_CONTINUE;
		}

		int rc = -1;
		stopwatch_t watch;
		uint64_t spent = 0;
		STOPWATCH(&watch, spent) {
			char *dir;
			mkdir(dir = pdu_string(pdu, 1), 0777); free(dir);
			mkdir(dir = pdu_string(pdu, 2), 0777); free(dir);

			char *type = pdu_string(pdu, 4);
			strings_t *args = NULL;

			if (strcmp(type, "COUNTER") == 0) {
				char *s = string("DS:x:GAUGE:120:U:U %s", creator->rras);
				args = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
				free(s);

			} else if (strcmp(type, "SAMPLE") == 0) {
				char *s = string(
						"DS:n:GAUGE:120:U:U "
						"DS:min:GAUGE:120:U:U "
						"DS:max:GAUGE:120:U:U "
						"DS:sum:GAUGE:120:U:U "
						"DS:mean:GAUGE:120:U:U "
						"DS:var:GAUGE:120:U:U "
						"%s", creator->rras);
				args = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
				free(s);

			} else if (strcmp(type, "RATE") == 0) {
				char *s = string("DS:value:GAUGE:120:U:U %s", creator->rras);
				args = strings_split(s, strlen(s), " ", SPLIT_NORMAL);
				free(s);

			} else {
				logger(LOG_WARNING, "creator[%i] rrdcreate %s %s failed: unknown metric type",
					creator->id, type, file);
			}

			if (args) {
#ifdef BOLO2RRD_DEBUG
				logger(LOG_DEBUG, "creator[%i] rrdcreate %07s %s",
						creator->id, type, file);
#endif

				rrd_clear_error();
				if (CACHED && rrdc_is_connected(CACHED)) {
					rc = rrdc_create_r2(file, 60, time_s() - 365 * 86400,
						0, NULL, NULL, args->num, (const char **)args->strings);
				} else {
					rc = rrd_create_r(file, 60, time_s() - 365 * 86400,
						args->num, (const char **)args->strings);
				}
				if (rc != 0)
					logger(LOG_WARNING, "creator[%i] rrdcreate %s %s failed: %s",
						creator->id, type, file, rrd_get_error());

				strings_free(args);
			}
		}

		if (rc == 0) {
			pdu_t *perf = pdu_make("SAMPLE", 1, "create.time.s");
			pdu_extendf(perf, "%lf", spent / 1000.);
			pdu_send_and_free(perf, creator->monitor);

			pdu_send_and_free(pdu_make("COUNT", 1, "create.ops"), creator->monitor);

		} else {
			pdu_send_and_free(pdu_make("COUNT", 1, "create.errors"), creator->monitor);
		}

		free(file);

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "creator[%i] unhandled socket!", creator->id);
	return VIGOR_REACTOR_HALT;
}
/* }}} */
static void * _creator_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	creator_t *creator = (creator_t*)_;

	int id = creator->id;

	rrd_get_context();
	if (CACHED && ! rrdc_is_connected(CACHED) && rrdc_connect(CACHED) != 0)
		logger(LOG_WARNING, "creator[%i]: failed to connect to cache daemon (%s): %s",
			id, CACHED, rrd_get_error());

	reactor_go(creator->reactor);

	logger(LOG_DEBUG, "creator[%i]: shutting down", id);

	zmq_close(creator->control);
	zmq_close(creator->monitor);
	zmq_close(creator->queue);

	if (CACHED && rrdc_is_connected(CACHED))
		rrdc_disconnect();

	reactor_free(creator->reactor);

	free(creator->rras);
	free(creator);

	logger(LOG_DEBUG, "creator[%i]: terminated", id);
	return NULL;
}
/* }}} */
int creator_thread(void *zmq, int id, const char *rras) /* {{{ */
{
	assert(zmq != NULL);

	creator_t *creator = vmalloc(sizeof(creator_t));
	creator->id = id;
	creator->rras = strdup(rras);

	int rc;

	logger(LOG_INFO, "initializing creator thread #%i", id);

	logger(LOG_DEBUG, "creator[%i] connecting creator.control -> supervisor", id);
	rc = bolo_subscriber_connect_supervisor(zmq, &creator->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "creator[%i] connecting creator.monitor -> monitor", id);
	rc = bolo_subscriber_connect_monitor(zmq, &creator->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "creator[%i] connecting creator.queue -> dispatcher", id);
	creator->queue = zmq_socket(zmq, ZMQ_PULL);
	if (!creator->queue)
		return -1;
	rc = zmq_connect(creator->queue, "inproc://bolo2rrd/v1/dispatcher.creates");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "creator[%i] setting up creator event reactor", id);
	creator->reactor = reactor_new();
	if (!creator->reactor)
		return -1;

	logger(LOG_DEBUG, "creator[%i] registering creator.control with event reactor", id);
	rc = reactor_set(creator->reactor, creator->control, _creator_reactor, creator);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "creator[%i] registering creator.queue with event reactor", id);
	rc = reactor_set(creator->reactor, creator->queue, _creator_reactor, creator);
	if (rc != 0)
		return rc;

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _creator_thread, creator);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */


static int _updater_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	updater_t *updater = (updater_t*)_;

	if (socket == updater->control) {
		if (strcmp(pdu_type(pdu), "TERMINATE") == 0)
			return VIGOR_REACTOR_HALT;
		logger(LOG_ERR, "updater[%i] thread received unrecognized [%s] PDU from control socket; ignoring",
			updater->id, pdu_type(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == updater->queue) {

		if (strcmp(pdu_type(pdu), "UPDATE") != 0) {
			logger(LOG_DEBUG, "updater[%i] discarding unhandled [%s] PDU", updater->id, pdu_type(pdu));
			return VIGOR_REACTOR_CONTINUE;
		}

		/*
		       +---------------+
		       | UPDATE        |
		       +---------------+
		       | /path/to/file |
		       | <TIMESTAMP>   |
		       | <TYPE>        |
		       | ...           |
		       +---------------+
		 */

		char *file = pdu_string(pdu, 1);
		struct stat st;
		if (stat(file, &st) != 0) {
			if (errno == ENOENT) {
				logger(LOG_DEBUG, "updater[%i] rrd %s does not exist; logging update.misdirect", updater->id);
				pdu_send_and_free(pdu_make("COUNT", 1, "update.misdirects"), updater->monitor);
			} else {
				logger(LOG_DEBUG, "updater[%i] rrd %s does not exist; logging error", updater->id);
				pdu_send_and_free(pdu_make("COUNT", 1, "update.errors"), updater->monitor);
			}
			free(file);
			return VIGOR_REACTOR_CONTINUE;
		}

		int data_frames = pdu_size(pdu) - 4;
		int rc = -1;

		stopwatch_t watch;
		uint64_t spent = 0;
		STOPWATCH(&watch, spent) {
			char *ts     = pdu_string(pdu, 2);
			char *type   = pdu_string(pdu, 3);
			char *update = NULL;

			if (strcmp(type, "COUNTER") == 0 && data_frames == 1) {

				char *value = pdu_string(pdu, 3 + 1);
				update = string("%s:%s", ts, value);
				free(value);


			} else if (strcmp(type, "SAMPLE") == 0 && data_frames == 6) {

				char *n    = pdu_string(pdu, 3 + 1);
				char *min  = pdu_string(pdu, 3 + 2);
				char *max  = pdu_string(pdu, 3 + 3);
				char *sum  = pdu_string(pdu, 3 + 4);
				char *mean = pdu_string(pdu, 3 + 5);
				char *var  = pdu_string(pdu, 3 + 6);
				update = string("%s:%s:%s:%s:%s:%s:%s", ts, n, min, max, sum, mean, var);
				free(n);
				free(min);
				free(max);
				free(sum);
				free(mean);
				free(var);


			} else if (strcmp(type, "RATE") == 0 && data_frames == 2) {

				/* ignore first frame, it's the window size */
				char *value = pdu_string(pdu, 3 + 2);
				update = string("%s:%s", ts, value);
				free(value);

			} else {
				logger(LOG_WARNING, "updater[%i] rrdupdate %s %s failed: unknown metric type (%i frames)",
					updater->id, type, file, data_frames);
			}


			if (update) {
				char *argv[2] = { update, NULL };

#ifdef BOLO2RRD_DEBUG
				logger(LOG_DEBUG, "updater[%i] rrdupdate %07s %s (%s)",
						updater->id, type, file, argv[0]);
#endif
				rrd_clear_error();
				if (CACHED && rrdc_is_connected(CACHED)) {
					rc = rrdc_update(file, 1, (const char **)argv);
				} else {
					rc = rrd_update_r(file, NULL, 1, (const char **)argv);
				}
				if (rc != 0)
					logger(LOG_WARNING, "updater[%i] rrdupdate %s %s (%s) failed: %s",
						updater->id, type, file, argv[0], rrd_get_error());

				free(argv[0]);
			}

			free(type);
			free(ts);
		}

		if (rc == 0) {
			pdu_t *perf = pdu_make("SAMPLE", 1, "update.time.s");
			pdu_extendf(perf, "%lf", spent / 1000.);
			pdu_send_and_free(perf, updater->monitor);

			pdu_send_and_free(pdu_make("COUNT", 1, "update.ops"), updater->monitor);

		} else {
			pdu_send_and_free(pdu_make("COUNT", 1, "update.errors"), updater->monitor);
		}

		free(file);

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "updater[%i] unhandled socket!", updater->id);
	return VIGOR_REACTOR_HALT;
}
/* }}} */
void * _updater_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	updater_t *updater = (updater_t*)_;

	int id = updater->id;

	rrd_get_context();
	if (CACHED && ! rrdc_is_connected(CACHED) && rrdc_connect(CACHED) != 0)
		logger(LOG_WARNING, "updater[%i]: failed to connect to cache daemon (%s): %s",
			id, CACHED, rrd_get_error());

	reactor_go(updater->reactor);

	logger(LOG_DEBUG, "updater[%i]: shutting down", id);

	zmq_close(updater->control);
	zmq_close(updater->monitor);
	zmq_close(updater->queue);

	if (CACHED && rrdc_is_connected(CACHED))
		rrdc_disconnect();

	reactor_free(updater->reactor);
	free(updater);

	logger(LOG_DEBUG, "updater[%i]: terminated", id);
	return NULL;
}
/* }}} */
int updater_thread(void *zmq, int id) /* {{{ */
{
	assert(zmq != NULL);

	int rc;

	updater_t *updater = vmalloc(sizeof(updater_t));
	updater->id = id;

	logger(LOG_INFO, "initializing updater thread #%i", id);

	logger(LOG_DEBUG, "updater[%i] connecting updater.control -> supervisor", id);
	rc = bolo_subscriber_connect_supervisor(zmq, &updater->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "updater[%i] connecting updater.monitor -> monitor", id);
	rc = bolo_subscriber_connect_monitor(zmq, &updater->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "updater[%i] connecting updater.queue -> dispatcher", id);
	updater->queue = zmq_socket(zmq, ZMQ_PULL);
	if (!updater->queue)
		return -1;
	rc = zmq_connect(updater->queue, "inproc://bolo2rrd/v1/dispatcher.updates");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "updater[%i] setting up updater event reactor", id);
	updater->reactor = reactor_new();
	if (!updater->reactor)
		return -1;

	logger(LOG_DEBUG, "updater[%i] registering updater.control with event reactor", id);
	rc = reactor_set(updater->reactor, updater->control, _updater_reactor, updater);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "updater[%i] registering updater.queue with event reactor", id);
	rc = reactor_set(updater->reactor, updater->queue, _updater_reactor, updater);
	if (rc != 0)
		return rc;

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _updater_thread, updater);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */


static char* s_rradefs(const char *filename) /* {{{ */
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
/* }}} */
int main(int argc, char **argv) /* {{{ */
{
	struct {
		char *endpoint;
		char *root;
		char *mapfile;

		int   verbose;
		int   daemonize;

		char *pidfile;
		char *user;
		char *group;

		int   updaters;
		int   creators;

		char *submit_to;
		char *prefix;
		int   schedule;
	} OPTIONS = {
		.verbose   = 0,
		.endpoint  = strdup("tcp://127.0.0.1:2997"),
		.submit_to = strdup("tcp://127.0.0.1:2999"),
		.root      = strdup("/var/lib/bolo/rrd"),
		.mapfile   = NULL, /* will be set later, based on root */
		.daemonize = 1,
		.pidfile   = strdup("/var/run/bolo2rrd.pid"),
		.user      = strdup("root"),
		.group     = strdup("root"),
		.creators  = 2,
		.updaters  = 8,
		.prefix    = NULL, /* will be set later, if needed */
		.schedule  = 60,
	};

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "version",          no_argument, NULL, 'V' },
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
		{ "schedule",   required_argument, NULL, 's' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:r:Fp:u:g:H:C:c:U:S:P:s:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf("bolo2rrd v%s\n", BOLO_VERSION);
			printf("Usage: bolo2rrd [-h?FVv] [-e tcp://host:port] [-P PREFIX] [-S tcp://host:port]\n"
			       "                [-U #] [-c #] [-r /path/to/root] [-H /path/to/map] [-C]\n"
			       "                [-u user] [-g group] [-p /path/to/pidfile]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -F, --foreground     don't daemonize, stay in the foreground\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -P, --prefix         prefix for submitting perf metrics back\n");
			printf("  -S, --submit         bolo listener endpoint for perf metrics\n");
			printf("  -U, --updaters       how many updater worker threads to run\n");
			printf("  -c, --creators       how many creator worker threads to run\n");
			printf("  -r, --root           root directory in which to store RRD files\n");
			printf("  -H, --hash           path to RRD map file\n");
			printf("  -s, --schedule       frequncy of bookkeeping jobs in seconds\n");
			printf("  -C, --rrdcached      address of rrdcached daemon to use\n");
			printf("  -u, --user           user to run as (if daemonized)\n");
			printf("  -g, --group          group to run as (if daemonized)\n");
			printf("  -p, --pidfile        where to store the pidfile (if daemonized)\n");
			exit(0);

		case 'V':
			printf("bolo2rrd v%s\n"
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
			free(OPTIONS.mapfile);
			OPTIONS.mapfile = strdup(optarg);
			break;

		case 'C':
			free(CACHED);
			CACHED = strdup(optarg);
			break;

		case 'U':
			OPTIONS.updaters = strtoll(optarg, NULL, 10);
			break;

		case 'c':
			OPTIONS.creators = strtoll(optarg, NULL, 10);
			break;

		case 's':
			OPTIONS.schedule = strtoll(optarg, NULL, 10);
			break;

		case 'S':
			free(OPTIONS.submit_to);
			OPTIONS.submit_to = strdup(optarg);
			break;

		case 'P':
			free(OPTIONS.prefix);
			OPTIONS.prefix = strdup(optarg);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	if (!OPTIONS.prefix) {
		char *s = fqdn();
		OPTIONS.prefix = string("%s:sys:bolo2rrd", s);
		free(s);
	}

	if (!OPTIONS.mapfile)
		OPTIONS.mapfile = string("%s/map", OPTIONS.root);

	struct stat st;
	if (stat(OPTIONS.root, &st) != 0) {
		fprintf(stderr, "%s: %s\n", OPTIONS.root, strerror(errno));
		exit(1);
	}

	if (OPTIONS.daemonize) {
		log_open("bolo2rrd", "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			exit(3);
		}
		umask(um);
	} else {
		log_open("bolo2rrd", "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	if (CACHED)
		logger(LOG_NOTICE, "using cache daemon at %s", CACHED);
	logger(LOG_NOTICE, "starting up");


	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		exit(1);
	}

	int rc;
	rc = bolo_subscriber_init();
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize subscriber architecture");
		exit(2);
	}

	rc = bolo_subscriber_monitor_thread(zmq, OPTIONS.prefix, OPTIONS.submit_to);
	if (rc != 0) {
		logger(LOG_ERR, "failed to spin up monitor thread");
		exit(2);
	}

	rc = bolo_subscriber_scheduler_thread(zmq, OPTIONS.schedule * 1000);
	if (rc != 0) {
		logger(LOG_ERR, "failed to spin up scheduler thread");
		exit(2);
	}

	rc = bolo_subscriber_metrics(zmq,
		"COUNT",  "create.ops",
		"COUNT",  "update.ops",
		"COUNT",  "create.errors",
		"COUNT",  "update.errors",
		"COUNT",  "create.misdirects",
		"COUNT",  "update.misdirects",

		"SAMPLE", "dispatch.time.s",
		"SAMPLE", "create.time.s",
		"SAMPLE", "update.time.s",
		NULL);
	if (rc != 0) {
		logger(LOG_WARNING, "failed to provision metrics");
		/* non-fatal, but still a problem... */
	}

	rc = dispatcher_thread(zmq, OPTIONS.endpoint, OPTIONS.root, OPTIONS.mapfile);
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize dispatcher: %s", strerror(errno));
		exit(2);
	}

	int i;
	logger(LOG_DEBUG, "setting up creator pool with %lu threads", OPTIONS.creators);
	char *rras = s_rradefs("/usr/lib/bolo/bolo2rrd/rra.def");
	for (i = 0; i < OPTIONS.creators; i++) {
		rc = creator_thread(zmq, i, rras);
		if (rc != 0) {
			logger(LOG_ERR, "failed to initialize creator #%i", i);
			exit(2);
		}
	}
	free(rras);

	logger(LOG_DEBUG, "setting up updater pool with %lu threads", OPTIONS.updaters);
	for (i = 0; i < OPTIONS.updaters; i++) {
		rc = updater_thread(zmq, i);
		if (rc != 0) {
			logger(LOG_ERR, "failed to initialize updater #%i", i);
			exit(2);
		}
	}

	bolo_subscriber_supervisor(zmq);

	free(OPTIONS.endpoint);
	free(OPTIONS.root);
	free(OPTIONS.mapfile);

	free(OPTIONS.pidfile);
	free(OPTIONS.user);
	free(OPTIONS.group);

	free(OPTIONS.submit_to);
	free(OPTIONS.prefix);

	return 0;
}
/* }}} */
