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
#include <signal.h>
#include <getopt.h>
#include <rrd.h>
#include <assert.h>

#define MON_SAMPLES_MAX 1024

#define MON_COUNT_CREATE_FALSE_POSITIVES  0
#define MON_COUNT_UPDATE_FALSE_POSITIVES  1
#define MON_COUNT_CREATE_ERRORS           2
#define MON_COUNT_UPDATE_ERRORS           3
#define MON_COUNT_CREATE_OPS              4
#define MON_COUNT_UPDATE_OPS              5
#define MON_COUNT_N                       6

const char *MON_COUNTS[] = {
	"create.false.positives",
	"update.false.positives",
	"create.errors",
	"update.errors",
	"create.ops",
	"update.ops",
};

#define MON_TIMING_DISPATCH               0
#define MON_TIMING_UPDATE                 1
#define MON_TIMING_CREATE                 2
#define MON_TIMING_N                      3

const char *MON_TIMINGS[] = {
	"dispatch",
	"update",
	"create",
};

typedef struct {
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

	char *rras;
} options_t;

/**************************/

typedef struct {
	char *parents[2];      /* absolute paths to the two parent directories */
	char *abspath;         /* absolute path to the RRD file */
	char *relpath;         /* "aa/bb/aabbccdd..." (without the .rrd suffix) */
	char *name;            /* metric name */
} rrdfile_t;

void rrdfile_free(rrdfile_t *file);
rrdfile_t *rrdfile_parse(const char *root, const char *sha1);
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
		sha1 = x++;

		rrdfile_t *file = rrdfile_parse(root, sha1);
		file->name = strdup(name);
		hash_set(&map->hash, name, file);
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
	free(file);
}
/* }}} */
rrdfile_t *rrdfile_parse(const char *root, const char *sha1) /* {{{ */
{
	rrdfile_t *file = vmalloc(sizeof(rrdfile_t));

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
	return rrdfile_parse(root, sha1.hex);
}
/* }}} */

/**************************/

typedef struct updater_t updater_t;
typedef struct creator_t creator_t;

typedef struct {
	void *ZMQ;
	void *command;    /* PUB:  command socket for controlling all other actors */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */
} supervisor_t;

int supervisor_init(supervisor_t *supervisor, void *ZMQ, options_t *options);
void * supervisor_thread(void *_);


typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */

	void *input;      /* PULL: receives metric submission requests from other actors */
	void *submission; /* PUSH: connected to bolo submission port (external) */

	reactor_t *reactor;

	char *prefix;

	struct {
		int      samples;
		uint64_t data[MON_SAMPLES_MAX];
	} timings[MON_TIMING_N];

	uint64_t counts[MON_COUNT_N];
} monitor_t;

int monitor_init(monitor_t *monitor, void *ZMQ, options_t *options);
void monitor_deinit(monitor_t *monitor);
int monitor_reactor(void *socket, pdu_t *pdu, void *_);
void * monitor_thread(void *_);

double monitor_median_value(monitor_t *monitor, int idx);
void monitor_sample(monitor_t *monitor, int idx, uint64_t value);
void monitor_count(monitor_t *monitor, int idx);

typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */

	void *subscriber; /* SUB:  subscriber to bolo broadcast port (external) */

	void *updates;    /* PUSH: fair-queue of RRD update requests */
	void *creates;    /* PUSH: fair-queue of RRD create requests */

	reactor_t *reactor;

	int   updater_pool_size;
	int   creator_pool_size;

	updater_t **updater_pool;
	creator_t **creator_pool;

	rrdmap_t *map;
	char     *root;
} dispatcher_t;

int dispatcher_init(dispatcher_t *dispatcher, void *ZMQ, options_t *options);
void dispatcher_deinit(dispatcher_t *dispatcher);
int dispatcher_reactor(void *socket, pdu_t *pdu, void *_);
void * dispatcher_thread(void *_);


typedef struct creator_t {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */

	void *queue;      /* PULL: hooked up to dispatcher.creates; receives RRD create requests */

	reactor_t *reactor;

	char *rras;
} creator_t;

int creator_init(creator_t *creator, void *ZMQ, options_t *options);
void creator_deinit(creator_t *creator);
int creator_reactor(void *socket, pdu_t *pdu, void *_);
void * creator_thread(void *_);


typedef struct updater_t {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */

	void *queue;      /* PULL: hooked up to dispatcher.updates; receives RRD update requests */

	reactor_t *reactor;
} updater_t;

int updater_init(updater_t *updater, void *ZMQ, options_t *options);
void updater_deinit(updater_t *updater);
int updater_reactor(void *socket, pdu_t *pdu, void *_);
void * updater_thread(void *_);


#define SCHEDULER_TICK   5000   /* ms */
typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; used to tick the monitoring */
} scheduler_t;

int scheduler_init(scheduler_t *scheduler, void *ZMQ, options_t *options);
void scheduler_deinit(scheduler_t *scheduler);
int scheduler_reactor(void *socket, pdu_t *pdu, void *_);
void * scheduler_thread(void *_);


int connect_to_monitor(void *ZMQ, void **zocket) /* {{{ */
{
	assert(ZMQ != NULL);
	assert(zocket != NULL);

	int rc;

	*zocket = zmq_socket(ZMQ, ZMQ_PUSH);
	if (!*zocket)
		return -1;

	rc = zmq_connect(*zocket, "inproc://bolo2rrd/v1/monitor.input");
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
int connect_to_supervisor(void *ZMQ, void **zocket) /* {{{ */
{
	assert(ZMQ != NULL);
	assert(zocket != NULL);

	int rc;

	*zocket = zmq_socket(ZMQ, ZMQ_SUB);
	if (!*zocket)
		return -1;

	rc = zmq_connect(*zocket, "inproc://bolo2rrd/v1/supervisor.command");
	if (rc != 0)
		return rc;

	rc = zmq_setsockopt(*zocket, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
int obey_supervisor(pdu_t *pdu) /* {{{ */
{
	if (strcmp(pdu_type(pdu), "@terminate") == 0)
		return VIGOR_REACTOR_HALT;
	else
		return VIGOR_REACTOR_CONTINUE;
}
/* }}} */
int block_signals_in_thread(void) /* {{{ */
{
	sigset_t blocked;
	sigfillset(&blocked);
	return pthread_sigmask(SIG_BLOCK, &blocked, NULL);
}
/* }}} */


int supervisor_init(supervisor_t *supervisor, void *ZMQ, options_t *options) /* {{{ */
{
	assert(supervisor != NULL);
	assert(ZMQ != NULL);
	assert(options != NULL);

	int rc;

	memset(supervisor, 0, sizeof(supervisor_t));
	supervisor->ZMQ = ZMQ;

	supervisor->command = zmq_socket(ZMQ, ZMQ_PUB);
	if (!supervisor->command)
		return -1;
	rc = zmq_bind(supervisor->command, "inproc://bolo2rrd/v1/supervisor.command");
	if (rc != 0)
		return rc;

	rc = connect_to_monitor(ZMQ, &supervisor->monitor);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
void supervisor_deinit(supervisor_t *supervisor) /* {{{ */
{
	assert(supervisor != NULL);

	zmq_close(supervisor->command);  supervisor->command = NULL;
	zmq_close(supervisor->monitor);  supervisor->monitor = NULL;
}
/* }}} */
void * supervisor_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	supervisor_t *supervisor = (supervisor_t*)_;

	sigset_t signals;
	sigemptyset(&signals);
	sigaddset(&signals, SIGTERM);
	sigaddset(&signals, SIGINT);

	int rc, sig;

	for (;;) {
		rc = sigwait(&signals, &sig);
		if (rc != 0) {
			logger(LOG_ERR, "sigwait: %s", strerror(errno));
			sig = SIGTERM; /* let's just pretend */
		}

		if (sig == SIGTERM || sig == SIGINT) {
			logger(LOG_INFO, "supervisor caught SIG%s; shutting down",
				sig == SIGTERM ? "TERM" : "INT");

			pdu_send_and_free(pdu_make("@terminate", 0), supervisor->command);
			supervisor_deinit(supervisor);

			logger(LOG_DEBUG, "destroying context");
			zmq_ctx_destroy(supervisor->ZMQ);

			logger(LOG_DEBUG, "exiting");
			exit(0);
		}

		logger(LOG_ERR, "ignoring unexpected signal %i\n", sig);
	}

	return NULL;
}
/* }}} */


int monitor_init(monitor_t *monitor, void *ZMQ, options_t *options) /* {{{ */
{
	assert(monitor != NULL);
	assert(ZMQ != NULL);
	assert(options != NULL);
	assert(options->submit_to != NULL);

	int rc;

	memset(monitor, 0, sizeof(monitor_t));
	monitor->prefix = strdup(options->prefix);

	rc = connect_to_supervisor(ZMQ, &monitor->control);
	if (rc != 0)
		return rc;

	monitor->input = zmq_socket(ZMQ, ZMQ_PULL);
	if (!monitor->input)
		return -1;
	rc = zmq_bind(monitor->input, "inproc://bolo2rrd/v1/monitor.input");
	if (rc != 0)
		return rc;

	monitor->submission = zmq_socket(ZMQ, ZMQ_PUSH);
	if (!monitor->submission)
		return -1;
	rc = vzmq_connect_af(monitor->submission, options->submit_to, AF_UNSPEC);
	if (rc != 0)
		return rc;

	monitor->reactor = reactor_new();
	if (!monitor->reactor)
		return -1;
	rc = reactor_set(monitor->reactor, monitor->control, monitor_reactor, monitor);
	if (rc != 0)
		return rc;
	rc = reactor_set(monitor->reactor, monitor->input, monitor_reactor, monitor);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
void monitor_deinit(monitor_t *monitor) /* {{{ */
{
	assert(monitor != NULL);

	zmq_close(monitor->control);     monitor->control    = NULL;
	zmq_close(monitor->submission);  monitor->submission = NULL;
	zmq_close(monitor->input);       monitor->input      = NULL;

	reactor_free(monitor->reactor);
	monitor->reactor = NULL;

	free(monitor->prefix);
	monitor->prefix = NULL;
}
/* }}} */
int monitor_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	monitor_t *monitor = (monitor_t*)_;

	if (socket == monitor->control) {
		return obey_supervisor(pdu);
	}

	if (socket == monitor->input) {
		if (strcmp(pdu_type(pdu), ".tick") == 0) {
			/*
			      +--------------+
			      | .tick        |
			      +--------------+
			 */

			logger(LOG_DEBUG, "submitting collected metrics");
			char *metric;

			int i;
			for (i = 0; i < MON_COUNT_N; i++) {
				metric = string("%s:sys:bolo2rrd:%s", monitor->prefix, MON_COUNTS[i]);
				pdu_send_and_free(
					counter_pdu(metric, monitor->counts[i]),
					monitor->submission);
				free(metric);
			}

			for (i = 0; i < MON_TIMING_N; i++) {
				double median = monitor_median_value(monitor, i) / 1001.;
				if (median > 1.0e4) {
					logger(LOG_ERR, "calculated median for %s at %lf, which seems suspiciously high; skipping",
						MON_TIMINGS[i], median);
					continue;
				}

				if (median < 0) {
					logger(LOG_ERR, "calculated negative mdiean for %s (%lf), which should be impossible; skipping",
						MON_TIMINGS[i], median);
					continue;
				}

				metric = string("%s:sys:bolo2rrd:%s.time.s", monitor->prefix, MON_TIMINGS[i]);
				pdu_send_and_free(sample_pdu(metric, 1, median), monitor->submission);
				free(metric);
			}

			memset(monitor->counts,  0, MON_COUNT_N  * sizeof(uint64_t));
			memset(monitor->timings, 0, MON_TIMING_N * sizeof(monitor->timings[0]));

			return VIGOR_REACTOR_CONTINUE;
		}

		if (strcmp(pdu_type(pdu), ".timing") == 0) {
			/*
			      +--------------+
			      | .timing      |
			      +--------------+
			      | <NAME>       | 1
			      | <TIME-IN-MS> | 2
			      +--------------+
			 */
			char *name = pdu_string(pdu, 1);

			int i;
			for (i = 0; i < MON_TIMING_N; i++) {
				if (strcmp(name, MON_TIMINGS[i]) == 0) {
					char *time = pdu_string(pdu, 2);
					uint64_t ms = strtoull(time, NULL, 10);
					free(time);

					monitor_sample(monitor, i, ms);
					break;
				}
			}

			free(name);
			return VIGOR_REACTOR_CONTINUE;
		}

		if (strcmp(pdu_type(pdu), ".count") == 0) {
			/*
			      +--------------+
			      | .count       |
			      +--------------+
			      | <NAME>       | 1
			      +--------------+
			 */

			char *name = pdu_string(pdu, 1);

			int i;
			for (i = 0; i < MON_COUNT_N; i++) {
				if (strcmp(name, MON_COUNTS[i]) == 0) {
					monitor_count(monitor, i);
					break;
				}
			}

			free(name);
			return VIGOR_REACTOR_CONTINUE;
		}

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
void * monitor_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	int rc;
	monitor_t *monitor = (monitor_t*)_;

	rc = block_signals_in_thread();
	if (rc != 0)
		exit(4);

	reactor_go(monitor->reactor);
	monitor_deinit(monitor);
	return NULL;
}
/* }}} */
int cmpul(const void *a, const void *b) { /* {{{ */
	return *(uint64_t *)a == *(uint64_t *)b ? 0
	     : *(uint64_t *)a >  *(uint64_t *)b ? 1 : -1;
}
/* }}} */
double monitor_median_value(monitor_t *monitor, int idx) /* {{{ */
{
	assert(monitor != NULL);
	assert(idx >= 0);
	assert(idx < MON_TIMING_N);

	int n = monitor->timings[idx].samples;
	if (n == 0) return 0.0;
	if (n > MON_SAMPLES_MAX) n = MON_SAMPLES_MAX;

	qsort(monitor->timings[idx].data, n, sizeof(uint64_t), cmpul);
	int mid = n / 2;

	if (n % 2 == 0)
		return (monitor->timings[idx].data[mid - 1] +
		        monitor->timings[idx].data[mid]) / 2.;

	return monitor->timings[idx].data[mid] * 1.;
}
/* }}} */
void monitor_sample(monitor_t *monitor, int idx, uint64_t value) /* {{{ */
{
	assert(monitor != NULL);
	assert(idx >= 0);
	assert(idx < MON_TIMING_N);

	if (monitor->timings[idx].samples < MON_SAMPLES_MAX) {
		monitor->timings[idx].data[monitor->timings[idx].samples++] = value;

	} else {
		int x = rand() / RAND_MAX * ++monitor->timings[idx].samples;
		if (x < MON_SAMPLES_MAX)
			monitor->timings[idx].data[x] = value;
	}
}
/* }}} */
void monitor_count(monitor_t *monitor, int idx) /* {{{ */
{
	assert(monitor != NULL);
	assert(idx >= 0);
	assert(idx < MON_COUNT_N);

	monitor->counts[idx]++;
}
/* }}} */


int dispatcher_init(dispatcher_t *dispatcher, void *ZMQ, options_t *options) /* {{{ */
{
	assert(dispatcher != NULL);
	assert(ZMQ != NULL);
	assert(options != NULL);
	assert(options->endpoint != NULL);
	assert(options->mapfile != NULL);

	int rc;

	memset(dispatcher, 0, sizeof(dispatcher_t));

	logger(LOG_DEBUG, "connecting dispatcher.control -> supervisor");
	rc = connect_to_supervisor(ZMQ, &dispatcher->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting dispatcher.monitor -> monitor");
	rc = connect_to_monitor(ZMQ, &dispatcher->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting dispatcher.subscriber -> bolo at %s", options->endpoint);
	dispatcher->subscriber = zmq_socket(ZMQ, ZMQ_SUB);
	if (!dispatcher->subscriber)
		return -1;
	rc = zmq_setsockopt(dispatcher->subscriber, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;
	rc = vzmq_connect_af(dispatcher->subscriber, options->endpoint, AF_UNSPEC);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "binding PUSH socket for updater pool");
	dispatcher->updates = zmq_socket(ZMQ, ZMQ_PUSH);
	if (!dispatcher->updates)
		return -1;
	rc = zmq_bind(dispatcher->updates, "inproc://bolo2rrd/v1/dispatcher.updates");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "binding PUSH socket for creator pool");
	dispatcher->creates = zmq_socket(ZMQ, ZMQ_PUSH);
	if (!dispatcher->creates)
		return -1;
	rc = zmq_bind(dispatcher->creates, "inproc://bolo2rrd/v1/dispatcher.creates");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "setting up dispatcher event reactor");
	dispatcher->reactor = reactor_new();
	if (!dispatcher->reactor)
		return -1;
	rc = reactor_set(dispatcher->reactor, dispatcher->control, dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;
	rc = reactor_set(dispatcher->reactor, dispatcher->subscriber, dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	dispatcher->root = strdup(options->root);
	dispatcher->updater_pool_size = options->updaters;
	dispatcher->creator_pool_size = options->creators;

	dispatcher->map = rrdmap_new(options->mapfile);
	rc = rrdmap_read(dispatcher->map, dispatcher->root);
	if (rc != 0)
		return rc;

	int i;

	logger(LOG_DEBUG, "setting up updater pool with %lu threads", dispatcher->updater_pool_size);
	dispatcher->updater_pool = vcalloc(dispatcher->updater_pool_size, sizeof(updater_t*));
	for (i = 0; i < dispatcher->updater_pool_size; i++) {
		dispatcher->updater_pool[i] = vmalloc(sizeof(updater_t));
		updater_init(dispatcher->updater_pool[i], ZMQ, options);
	}

	logger(LOG_DEBUG, "setting up creator pool with %lu threads", dispatcher->creator_pool_size);
	dispatcher->creator_pool = vcalloc(dispatcher->creator_pool_size, sizeof(creator_t*));
	for (i = 0; i < dispatcher->creator_pool_size; i++) {
		dispatcher->creator_pool[i] = vmalloc(sizeof(creator_t));
		creator_init(dispatcher->creator_pool[i], ZMQ, options);
	}

	return 0;
}
/* }}} */
void dispatcher_deinit(dispatcher_t *dispatcher) /* {{{ */
{
	assert(dispatcher != NULL);

	free(dispatcher->root);
	rrdmap_free(dispatcher->map);

	zmq_close(dispatcher->subscriber);  dispatcher->subscriber = NULL;
	zmq_close(dispatcher->updates);     dispatcher->updates    = NULL;
	zmq_close(dispatcher->creates);     dispatcher->creates    = NULL;
	zmq_close(dispatcher->monitor);     dispatcher->monitor    = NULL;
	zmq_close(dispatcher->control);     dispatcher->control    = NULL;

	reactor_free(dispatcher->reactor);
	dispatcher->reactor = NULL;

	int i;
	for (i = 0; i < dispatcher->updater_pool_size; i++)
		dispatcher->updater_pool[i] = NULL;
	free(dispatcher->updater_pool);
	dispatcher->updater_pool = NULL;

	for (i = 0; i < dispatcher->creator_pool_size; i++)
		dispatcher->creator_pool[i] = NULL;
	free(dispatcher->creator_pool);
	dispatcher->creator_pool = NULL;
}
/* }}} */
int dispatcher_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	dispatcher_t *dispatcher = (dispatcher_t*)_;

	if (socket == dispatcher->control) {
		return obey_supervisor(pdu);
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

			pdu_t *perf = pdu_make(".timing", 1, "dispatch");
			pdu_extendf(perf, "%lu", spent);
			pdu_send_and_free(perf, dispatcher->monitor);
		}

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
void * dispatcher_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	int rc;
	dispatcher_t *dispatcher = (dispatcher_t*)_;
	pthread_t tid;

	rc = block_signals_in_thread();
	if (rc != 0)
		exit(5);

	int i;
	logger(LOG_DEBUG, "spinning up updater pool with %lu threads", dispatcher->updater_pool_size);
	for (i = 0; i < dispatcher->updater_pool_size; i++) {
		logger(LOG_INFO, "spinning up updater thread #%i (%p)", i+1, dispatcher->updater_pool[i]);
		rc = pthread_create(&tid, NULL, updater_thread, dispatcher->updater_pool[i]);
		if (rc != 0)
			exit(5);
	}

	logger(LOG_DEBUG, "spinning up creator pool with %lu threads", dispatcher->creator_pool_size);
	for (i = 0; i < dispatcher->creator_pool_size; i++) {
		logger(LOG_INFO, "spinning up creator thread #%i (%p)", i+1, dispatcher->creator_pool[i]);
		rc = pthread_create(&tid, NULL, creator_thread, dispatcher->creator_pool[i]);
		if (rc != 0)
			exit(5);
	}

	reactor_go(dispatcher->reactor);
	dispatcher_deinit(dispatcher);
	return NULL;
}
/* }}} */


int creator_init(creator_t *creator, void *ZMQ, options_t *options) /* {{{ */
{
	assert(creator != NULL);
	assert(ZMQ != NULL);

	int rc;

	logger(LOG_DEBUG, "connecting creator.control -> supervisor");
	rc = connect_to_supervisor(ZMQ, &creator->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting creator.monitor -> monitor");
	rc = connect_to_monitor(ZMQ, &creator->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting creator.queue -> dispatcher");
	creator->queue = zmq_socket(ZMQ, ZMQ_PULL);
	if (!creator->queue)
		return -1;
	rc = zmq_connect(creator->queue, "inproc://bolo2rrd/v1/dispatcher.creates");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "setting up updater event reactor");
	creator->reactor = reactor_new();
	if (!creator->reactor)
		return -1;
	rc = reactor_set(creator->reactor, creator->control, creator_reactor, creator);
	if (rc != 0)
		return rc;
	rc = reactor_set(creator->reactor, creator->queue, creator_reactor, creator);
	if (rc != 0)
		return rc;

	creator->rras = strdup(options->rras);

	return 0;
}
/* }}} */
void creator_deinit(creator_t *creator) /* {{{ */
{
	assert(creator != NULL);

	zmq_close(creator->control);  creator->control = NULL;
	zmq_close(creator->monitor);  creator->monitor = NULL;
	zmq_close(creator->queue);    creator->queue   = NULL;

	free(creator->rras);
	creator->rras = NULL;

	reactor_free(creator->reactor);
	creator->reactor = NULL;

	free(creator);
	creator = NULL;
}
/* }}} */
int creator_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	creator_t *creator = (creator_t*)_;

	if (socket == creator->control) {
		return obey_supervisor(pdu);
	}

	if (socket == creator->queue) {

		if (strcmp(pdu_type(pdu), "CREATE") != 0) {
			logger(LOG_DEBUG, "discarding unhandled [%s] PDU", pdu_type(pdu));
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
			logger(LOG_DEBUG, "rrd %s already exists; logging false positive");
			pdu_send_and_free(pdu_make(".count", 1, "create.false.positives"), creator->monitor);
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
				logger(LOG_WARNING, "rrdcreate %s %s failed: unknown metric type", type, file);
			}

			if (args) {
				rrd_clear_error();
				rc = rrd_create_r(file, 60, time_s() - 365 * 86400,
					args->num, (const char **)args->strings);
				strings_free(args);

				if (rc != 0)
					logger(LOG_WARNING, "rrdcreate %s %s failed: %s",
						type, file, rrd_strerror(errno));
			}
		}

		if (rc == 0) {
			pdu_t *perf = pdu_make(".timing", 1, "create");
			pdu_extendf(perf, "%lu", spent);
			pdu_send_and_free(perf, creator->monitor);

			pdu_send_and_free(pdu_make(".count", 1, "create.ops"), creator->monitor);

		} else {
			pdu_send_and_free(pdu_make(".count", 1, "create.errors"), creator->monitor);
		}

		free(file);

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
void * creator_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	creator_t *creator = (creator_t*)_;

	reactor_go(creator->reactor);
	creator_deinit(creator);
	return NULL;
}
/* }}} */


int updater_init(updater_t *updater, void *ZMQ, options_t *options) /* {{{ */
{
	assert(updater != NULL);
	assert(ZMQ != NULL);

	int rc;

	logger(LOG_DEBUG, "connecting updater.control -> supervisor");
	rc = connect_to_supervisor(ZMQ, &updater->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting updater.monitor -> monitor");
	rc = connect_to_monitor(ZMQ, &updater->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting updater.queue -> dispatcher");
	updater->queue = zmq_socket(ZMQ, ZMQ_PULL);
	if (!updater->queue)
		return -1;
	rc = zmq_connect(updater->queue, "inproc://bolo2rrd/v1/dispatcher.updates");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "setting up updater event reactor");
	updater->reactor = reactor_new();
	if (!updater->reactor)
		return -1;
	rc = reactor_set(updater->reactor, updater->control, updater_reactor, updater);
	if (rc != 0)
		return rc;
	rc = reactor_set(updater->reactor, updater->queue, updater_reactor, updater);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
void updater_deinit(updater_t *updater) /* {{{ */
{
	assert(updater != NULL);

	zmq_close(updater->control);  updater->control = NULL;
	zmq_close(updater->monitor);  updater->monitor = NULL;
	zmq_close(updater->queue);    updater->queue   = NULL;

	reactor_free(updater->reactor);
	updater->reactor = NULL;

	free(updater);
	updater = NULL;
}
/* }}} */
int updater_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	updater_t *updater = (updater_t*)_;

	if (socket == updater->control) {
		return obey_supervisor(pdu);
	}

	if (socket == updater->queue) {

		if (strcmp(pdu_type(pdu), "UPDATE") != 0) {
			logger(LOG_DEBUG, "discarding unhandled [%s] PDU", pdu_type(pdu));
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
				logger(LOG_DEBUG, "rrd %s does not exist; logging false positive");
				pdu_send_and_free(pdu_make(".count", 1, "update.false.positives"), updater->monitor);
			} else {
				logger(LOG_DEBUG, "rrd %s does not exist; logging error");
				pdu_send_and_free(pdu_make(".count", 1, "update.errors"), updater->monitor);
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
				logger(LOG_WARNING, "rrdupdate %s %s failed: unknown metric type (%i frames)",
					type, file, data_frames);
			}


			if (update) {
				char *argv[2] = { update, NULL };

				rrd_clear_error();
				rc = rrd_update_r(file, NULL, 1, (const char **)argv);

				if (rc != 0)
					logger(LOG_WARNING, "rrdupdate %s %s (%s) failed: %s",
						type, file, argv[0], rrd_strerror(errno));

				free(argv[0]);

			}

			free(type);
			free(ts);
		}

		if (rc == 0) {
			pdu_t *perf = pdu_make(".timing", 1, "update");
			pdu_extendf(perf, "%lu", spent);
			pdu_send_and_free(perf, updater->monitor);

			pdu_send_and_free(pdu_make(".count", 1, "update.ops"), updater->monitor);

		} else {
			pdu_send_and_free(pdu_make(".count", 1, "update.errors"), updater->monitor);
		}

		free(file);

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
void * updater_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	updater_t *updater = (updater_t*)_;

	rrd_get_context();

	reactor_go(updater->reactor);
	updater_deinit(updater);
	return NULL;
}
/* }}} */


int scheduler_init(scheduler_t *scheduler, void *ZMQ, options_t *options) /* {{{ */
{
	assert(scheduler != NULL);
	assert(ZMQ != NULL);

	int rc;

	logger(LOG_DEBUG, "connecting scheduler.control -> supervisor");
	rc = connect_to_supervisor(ZMQ, &scheduler->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting scheduler.monitor -> monitor");
	rc = connect_to_monitor(ZMQ, &scheduler->monitor);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
void scheduler_deinit(scheduler_t *scheduler) /* {{{ */
{
	assert(scheduler != NULL);

	zmq_close(scheduler->control);  scheduler->control = NULL;
	zmq_close(scheduler->monitor);  scheduler->monitor = NULL;
}
/* }}} */
void * scheduler_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	scheduler_t *scheduler = (scheduler_t*)_;

	int rc;
	zmq_pollitem_t poller[1] = { { scheduler->control, 0, ZMQ_POLLIN } };
	while ((rc = zmq_poll(poller, 1, SCHEDULER_TICK)) >= 0) {
		if (rc == 0) {
			pdu_send_and_free(pdu_make(".tick", 0), scheduler->monitor);
			continue;
		}

		pdu_t *pdu = pdu_recv(scheduler->control);
		if (obey_supervisor(pdu) == VIGOR_REACTOR_HALT) {
			pdu_free(pdu);
			break;
		}
		pdu_free(pdu);
	}

	scheduler_deinit(scheduler);
	return NULL;
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
options_t* parse_options(int argc, char **argv) /* {{{ */
{
	options_t *options = vmalloc(sizeof(options_t));

	options->verbose   = 0;
	options->endpoint  = strdup("tcp://127.0.0.1:2997");
	options->submit_to = strdup("tcp://127.0.0.1:2999");
	options->root      = strdup("/var/lib/bolo/rrd");
	options->mapfile   = NULL; /* will be set later, based on root */
	options->daemonize = 1;
	options->pidfile   = strdup("/var/run/bolo2rrd.pid");
	options->user      = strdup("root");
	options->group     = strdup("root");
	options->creators  = 2;
	options->updaters  = 8;
	options->prefix    = NULL; /* will be set later, if needed */

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
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+e:r:Fp:u:g:H:C:c:U:S:P:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			/* FIXME: need help text! */
			break;

		case 'v':
			options->verbose++;
			break;

		case 'e':
			free(options->endpoint);
			options->endpoint = strdup(optarg);
			break;

		case 'r':
			free(options->root);
			options->root = strdup(optarg);
			break;

		case 'F':
			options->daemonize = 0;
			break;

		case 'p':
			free(options->pidfile);
			options->pidfile = strdup(optarg);
			break;

		case 'u':
			free(options->user);
			options->user = strdup(optarg);
			break;

		case 'g':
			free(options->group);
			options->group = strdup(optarg);
			break;

		case 'H':
			free(options->mapfile);
			options->mapfile = strdup(optarg);
			break;

		case 'C':
			setenv("RRDCACHED_ADDRESS", optarg, 1);
			break;

		case 'U':
			options->updaters = strtoll(optarg, NULL, 10);
			break;

		case 'c':
			options->creators = strtoll(optarg, NULL, 10);
			break;

		case 'S':
			free(options->submit_to);
			options->submit_to = strdup(optarg);
			break;

		case 'P':
			free(options->prefix);
			options->prefix = strdup(optarg);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	if (!options->prefix)
		options->prefix = fqdn();

	if (!options->mapfile)
		options->mapfile = string("%s/map", options->root);

	struct stat st;
	if (stat(options->root, &st) != 0) {
		fprintf(stderr, "%s: %s\n", options->root, strerror(errno));
		exit(1);
	}

	options->rras = s_rradefs("/usr/lib/bolo/bolo2rrd/rra.def");

	return options;
}
/* }}} */
int main(int argc, char **argv) /* {{{ */
{
	options_t *options = parse_options(argc, argv);


	if (options->daemonize) {
		log_open("bolo2rrd", "daemon");
		log_level(LOG_ERR + options->verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(options->pidfile, options->user, options->group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			exit(3);
		}
		umask(um);
	} else {
		log_open("bolo2rrd", "console");
		log_level(LOG_INFO + options->verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");


	void *ZMQ = zmq_ctx_new();
	if (!ZMQ) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		exit(1);
	}

	monitor_t MONITOR;
	if (monitor_init(&MONITOR, ZMQ, options) != 0) {
		logger(LOG_ERR, "failed to initialize monitor: %s", strerror(errno));
		exit(2);
	}

	supervisor_t SUPERVISOR;
	if (supervisor_init(&SUPERVISOR, ZMQ, options) != 0) {
		logger(LOG_ERR, "failed to initialize supervisor: %s", strerror(errno));
		exit(2);
	}

	dispatcher_t DISPATCHER;
	if (dispatcher_init(&DISPATCHER, ZMQ, options) != 0) {
		logger(LOG_ERR, "failed to initialize dispatcher: %s", strerror(errno));
		exit(2);
	}

	scheduler_t SCHEDULER;
	if (scheduler_init(&SCHEDULER, ZMQ, options) != 0) {
		logger(LOG_ERR, "failed to initialize scheduler: %s", strerror(errno));
		exit(2);
	}

	pthread_t tid;
	pthread_create(&tid, NULL, monitor_thread,    &MONITOR);
	pthread_create(&tid, NULL, dispatcher_thread, &DISPATCHER);
	pthread_create(&tid, NULL, scheduler_thread,  &SCHEDULER);
	supervisor_thread(&SUPERVISOR);
	pthread_exit(0);
	return 0;
}
/* }}} */
