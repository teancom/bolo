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
#include <sys/mman.h>
#include <signal.h>
#include <assert.h>

typedef struct {
	void *control;    /* SUB:    hooked up to supervisor.command; receives control messages */
	void *tock;       /* SUB:    hooked up to scheduler.tick, for timing interrupts */

	void *listener;   /* PULL:   bound to external interface for metric / state submission */
	void *broadcast;  /* PUB:    bound to external interface for broadcasting updates */
	void *management; /* ROUTER: bound to external interface for management purposes */

	reactor_t *reactor;
	server_t  *server;

	struct {
		int32_t last;     /* s */
		int16_t interval; /* s */
	} freshness, savestate, tick;
} kernel_t;

typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */

	void *tick;       /* PUB:  broadcasts timing interrupts */
	int   interval;
} scheduler_t;

#define winstart(x, t) ((t) - ((t) % (x)->window->time))
#define winend(x, t)   (winstart((x), (t)) + (x)->window->time)
#define max(a,b) ((a) > (b) ? (a) : (b))
#define min(a,b) ((a) < (b) ? (a) : (b))

static void broadcast_state(kernel_t*, state_t*);
static void broadcast_transition(kernel_t*, state_t*);
static void broadcast_event(kernel_t*, event_t*);
static void broadcast_counter(kernel_t *kernel, counter_t *counter);
static void broadcast_sample(kernel_t *kernel, sample_t *sample);
static void broadcast_rate(kernel_t *kernel, rate_t *rate);

static int save_keys(hash_t *keys, const char *file);
static int read_keys(hash_t *keys, const char *file);

static void check_freshness(kernel_t *kernel);

static void event_free(event_t *ev);
static void buffer_event(db_t *db, event_t *ev, int max, int keep);

static inline const char *statstr(uint8_t s)
{
	static const char *names[] = { "OK", "WARNING", "CRITICAL", "UNKNOWN" };
	return names[s > 3 ? 3 : s];
}

/*************************************************************************/

static void broadcast_state(kernel_t *kernel, state_t *state) /* {{{ */
{
	logger(LOG_INFO, "broadcasting [STATE] data for %s: "
		"ts=%i, stale=%s, status=%i, summary=%s",
		state->name, state->last_seen, state->stale ? "y" : "n",
		state->status, state->summary);

	pdu_t *p = pdu_make("STATE", 1, state->name);
	pdu_extendf(p, "%li", state->last_seen);
	pdu_extendf(p, "%s",  state->stale ? "stale" : "fresh");
	pdu_extendf(p, "%s",  statstr(state->status));
	pdu_extendf(p, "%s",  state->summary);
	pdu_send_and_free(p, kernel->broadcast);
}
/* }}} */
static void broadcast_transition(kernel_t *kernel, state_t *state) /* {{{ */
{
	logger(LOG_INFO, "broadcasting [TRANSITION] data for %s: "
		"ts=%i, stale=%s, status=%i, summary=%s",
		state->name, state->last_seen, state->stale ? "y" : "n",
		state->status, state->summary);

	pdu_t *p = pdu_make("TRANSITION", 1, state->name);
	pdu_extendf(p, "%li", state->last_seen);
	pdu_extendf(p, "%s",  state->stale ? "stale" : "fresh");
	pdu_extendf(p, "%s",  statstr(state->status));
	pdu_extendf(p, "%s",  state->summary);
	pdu_send_and_free(p, kernel->broadcast);
}
/* }}} */
static void broadcast_event(kernel_t *kernel, event_t *ev) /* {{{ */
{
	logger(LOG_INFO, "broadcasting [EVENT] data for %s: ts=%i, extra='%s'",
		ev->name, ev->timestamp, ev->extra);

	pdu_t *p = pdu_make("EVENT", 0);
	pdu_extendf(p, "%i", ev->timestamp);
	pdu_extendf(p, "%s", ev->name);
	pdu_extendf(p, "%s", ev->extra);
	pdu_send_and_free(p, kernel->broadcast);
}
/* }}} */
static void broadcast_counter(kernel_t *kernel, counter_t *counter) /* {{{ */
{
	int32_t ts = winstart(counter, counter->last_seen);

	logger(LOG_INFO, "broadcasting [COUNTER] data for %s: "
		"ts=%i, value=%i",
		counter->name, ts, counter->value);

	pdu_t *p = pdu_make("COUNTER", 0);
	pdu_extendf(p, "%u",  ts);
	pdu_extendf(p, "%s",  counter->name);
	pdu_extendf(p, "%lu", counter->value);
	pdu_send_and_free(p, kernel->broadcast);
}
/* }}} */
static void broadcast_sample(kernel_t *kernel, sample_t *sample) /* {{{ */
{
	int32_t ts = winstart(sample, sample->last_seen);

	logger(LOG_INFO, "broadcasting [SAMPLE] data for %s: "
		"ts=%i, n=%i, min=%e, max=%e, sum=%e, mean=%e, var=%e",
		sample->name, ts, sample->n, sample->min,
		sample->max, sample->sum, sample->mean, sample->var);

	pdu_t *p = pdu_make("SAMPLE", 0);
	pdu_extendf(p, "%u", ts);
	pdu_extendf(p, "%s", sample->name);
	pdu_extendf(p, "%u", sample->n);
	pdu_extendf(p, "%e", sample->min);
	pdu_extendf(p, "%e", sample->max);
	pdu_extendf(p, "%e", sample->sum);
	pdu_extendf(p, "%e", sample->mean);
	pdu_extendf(p, "%e", sample->var);
	pdu_send_and_free(p, kernel->broadcast);
}
/* }}} */
static void broadcast_rate(kernel_t *kernel, rate_t *rate) /* {{{ */
{
	int32_t ts = winstart(rate, rate->last_seen);
	double value = rate_calc(rate, rate->window->time);

	logger(LOG_INFO, "broadcasting [RATE] data for %s: "
		"ts=%i, first=%lu, last=%lu, per/%li=%e",
		rate->name, ts, rate->first, rate->last, rate->window->time, value);

	pdu_t *p = pdu_make("RATE", 0);
	pdu_extendf(p, "%u", ts);
	pdu_extendf(p, "%s", rate->name);
	pdu_extendf(p, "%i", rate->window->time);
	pdu_extendf(p, "%e", value);
	pdu_send_and_free(p, kernel->broadcast);
}
/* }}} */

static int save_keys(hash_t *keys, const char *file) /* {{{ */
{
	FILE *io = fopen(file, "w");
	if (!io) {
		logger(LOG_ERR, "kernel failed to open keys file %s for writing: %s",
				file, strerror(errno));
		return -1;
	}

	logger(LOG_NOTICE, "saving keys to %s", file);
	fprintf(io, "# generated %lu\n", time_ms());

	unsigned long n = 0;
	char *key, *value;
	for_each_key_value(keys, key, value) {
		if (!value) continue;
		fprintf(io, "%s = %s\n", key, value);
		n++;
	}
	fprintf(io, "# %lu keys\n", n);
	fclose(io);
	return 0;
}
/* }}} */
static int read_keys(hash_t *keys, const char *file) /* {{{ */
{
	FILE *io = fopen(file, "r");
	if (!io) {
		logger(LOG_ERR, "kernel failed to open %s for reading: %s",
			file, strerror(errno));
		return -1;
	}

	logger(LOG_NOTICE, "reading keys from %s", file);

	char buf[8192];
	while (fgets(buf, 8192, io)) {
		char *key, *value, *nl;
		key = buf;

		while (*key && isspace(*key)) key++;
		if (*key == '#') continue;

		value = key;
		while (*value && !isspace(*value)) value++;
		*value++ = '\0';
		while (*value && isspace(*value)) value++;
		if (*value++ != '=') continue;
		while (*value && isspace(*value)) value++;

		nl = value;
		while (*nl && *nl != '\n') nl++;
		*nl = '\0';

		value = strdup(value);
		char *existing = hash_set(keys, key, value);
		if (existing != value)
			free(existing);
	}

	fclose(io);
	return 0;
}
/* }}} */

static void check_freshness(kernel_t *kernel) /* {{{ */
{
	state_t *state;
	char *key;
	int32_t now = time_s();

	logger(LOG_INFO, "checking freshness");
	for_each_key_value(&kernel->server->db.states, key, state) {
		if (state->expiry > now) continue;

		int transition = !state->stale || state->status != state->type->status;

		logger(LOG_INFO, "state %s is stale; marking", key);
		state->stale   = 1;
		state->expiry  = now + state->type->freshness;
		state->status  = state->type->status;
		free(state->summary);
		state->summary = strdup(state->type->summary);

		if (transition)
			broadcast_transition(kernel, state);
		broadcast_state(kernel, state);
	}
}
/* }}} */

static void event_free(event_t *ev) /* {{{ */
{
	if (!ev) return;
	list_delete(&ev->l);
	free(ev->name);
	free(ev->extra);
	free(ev);
}
/* }}} */
static void buffer_event(db_t *db, event_t *ev, int max, int keep) /* {{{ */
{
	if (max > 0) {
		list_push(&db->events, &ev->l);
		db->events_count++;

		if (keep == EVENTS_KEEP_NUMBER) {
			while (db->events_count > max) {
				event_free(list_head(&db->events, event_t, l));
				db->events_count--;
			}

		} else {
			int32_t now  = ev->timestamp;

			for (;;) {
				ev = list_head(&db->events, event_t, l);
				if (ev->timestamp > now - max) break;
				event_free(ev);
				db->events_count--;
			}
		}
	}
}
/* }}} */

/*************************************************************************/

static int core_connect_scheduler(void *zmq, void **zocket) /* {{{ */
{
	assert(zmq != NULL);
	assert(zocket != NULL);

	int rc;

	*zocket = zmq_socket(zmq, ZMQ_SUB);
	if (!*zocket)
		return -1;

	rc = zmq_connect(*zocket, "inproc://bolo/v1/scheduler.tick");
	if (rc != 0)
		return rc;

	rc = zmq_setsockopt(*zocket, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
static int core_connect_supervisor(void *zmq, void **zocket) /* {{{ */
{
	assert(zmq != NULL);
	assert(zocket != NULL);

	int rc;

	*zocket = zmq_socket(zmq, ZMQ_SUB);
	if (!*zocket)
		return -1;

	rc = zmq_connect(*zocket, "inproc://bolo/v1/supervisor.command");
	if (rc != 0)
		return rc;

	rc = zmq_setsockopt(*zocket, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

/*************************************************************************/

static void * _scheduler_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	scheduler_t *scheduler = (scheduler_t*)_;

	int rc;
	zmq_pollitem_t poller[1] = { { scheduler->control, 0, ZMQ_POLLIN } };
	while ((rc = zmq_poll(poller, 1, scheduler->interval)) >= 0) {
		if (rc == 0) {
			logger(LOG_DEBUG, "scheduler tocked (%i); sending broadcast [TOCK] PDU", scheduler->interval);
			pdu_send_and_free(pdu_make("TOCK", 0), scheduler->tick);
			continue;
		}

		pdu_t *pdu = pdu_recv(scheduler->control);
		/* FIXME: any message from supervisor.control == exit! */
		pdu_free(pdu);
		break;
	}

	logger(LOG_DEBUG, "scheduler: shutting down");

	zmq_close(scheduler->control);
	zmq_close(scheduler->tick);
	free(scheduler);

	logger(LOG_DEBUG, "scheduler: terminated");

	return NULL;
}
/* }}} */
int core_scheduler_thread(void *zmq, int interval) /* {{{ */
{
	assert(zmq != NULL);
	assert(interval > 0);

	int rc;
	scheduler_t *scheduler = vmalloc(sizeof(scheduler_t));
	scheduler->interval = interval;

	logger(LOG_DEBUG, "connecting scheduler.control -> supervisor");
	rc = core_connect_supervisor(zmq, &scheduler->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "binding PUB socket for scheduler.tick");
	scheduler->tick = zmq_socket(zmq, ZMQ_PUB);
	if (!scheduler->tick)
		return -1;
	rc = zmq_bind(scheduler->tick, "inproc://bolo/v1/scheduler.tick");
	if (rc != 0)
		return rc;

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _scheduler_thread, scheduler);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

/*************************************************************************/

static int _pdu_is(pdu_t *pdu, const char *type, int min, int max)
{
	assert(pdu != NULL);
	assert(type != NULL);

	if (min && pdu_size(pdu) < min) return 0;
	if (max && pdu_size(pdu) > max) return 0;
	return strcmp(pdu_type(pdu), type) == 0;
}

static void * _kernel_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	kernel_t *kernel = (kernel_t*)_;
	reactor_go(kernel->reactor);

	logger(LOG_DEBUG, "kernel: shutting down");

	zmq_close(kernel->control);
	zmq_close(kernel->tock);
	if (kernel->listener)   zmq_close(kernel->listener);
	if (kernel->broadcast)  zmq_close(kernel->broadcast);
	if (kernel->management) zmq_close(kernel->management);

	reactor_free(kernel->reactor);
	deconfigure(kernel->server);
	free(kernel->server);
	free(kernel);

	logger(LOG_DEBUG, "kernel: terminated");
	return NULL;
}
/* }}} */
static int _kernel_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	kernel_t *kernel = (kernel_t*)_;

	if (socket == kernel->control) {
		logger(LOG_DEBUG, "received TERMINATE");
		return VIGOR_REACTOR_HALT;
	}

	/* {{{ kernel.tock timing interrupts */
	if (socket == kernel->tock) {
		int32_t now = time_s();
		logger(LOG_DEBUG, "kernel received TICK from scheduler at %i", now);

		if (kernel->tick.last + kernel->tick.interval < now) {
			kernel->tick.last = now;

			char *name;
			int32_t ts = now - 15; /* FIXME: make configurable */

			counter_t *counter;
			for_each_key_value(&kernel->server->db.counters, name, counter) {
				if (counter->last_seen == 0 || winend(counter, counter->last_seen) >= ts)
					continue;
				broadcast_counter(kernel, counter);
				counter_reset(counter);
			}

			sample_t *sample;
			for_each_key_value(&kernel->server->db.samples, name, sample) {
				if (sample->last_seen == 0 || winend(sample, sample->last_seen) >= ts)
					continue;
				broadcast_sample(kernel, sample);
				sample_reset(sample);
			}
			rate_t *rate;
			for_each_key_value(&kernel->server->db.rates, name, rate) {
				if (rate->last_seen == 0 || winend(rate, rate->last_seen) >= ts)
					continue;
				broadcast_rate(kernel, rate);
				rate_reset(rate);
			}
		}

		if (kernel->freshness.last + kernel->freshness.interval < now) {
			kernel->freshness.last = now;

			check_freshness(kernel);
		}

		if (kernel->savestate.last + kernel->savestate.interval < now) {
			kernel->savestate.last = now;

			binf_write(&kernel->server->db, kernel->server->config.savefile);
			save_keys(&kernel->server->keys, kernel->server->config.keysfile);
		}

		return VIGOR_REACTOR_CONTINUE;
	}
	/* }}} */

	if (socket == kernel->management) {
		/* [ STATE | name ] {{{ */
		if (_pdu_is(pdu, "STATE", 2, 2)) {
			char *name = pdu_string(pdu, 1);
			state_t *state = hash_get(&kernel->server->db.states, name);
			if (!state) {
				pdu_send_and_free(pdu_reply(pdu, "ERROR", 1, "State Not Found"), socket);
			} else {
				pdu_t *a = pdu_reply(pdu, "STATE", 1, name);
				pdu_extendf(a, "%li", state->last_seen);
				pdu_extendf(a, "%s",  state->stale ? "stale" : "fresh");
				pdu_extendf(a, "%s",  statstr(state->status));
				pdu_extendf(a, "%s",  state->summary);
				pdu_send_and_free(a, socket);
			}
			free(name);
			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ DUMP ] {{{ */
		if (_pdu_is(pdu, "DUMP", 1, 1)) {
			FILE *io = tmpfile();
			if (!io) {
				logger(LOG_ERR, "kernel cannot dump state; unable to create temporary file: %s", strerror(errno));
				pdu_send_and_free(pdu_reply(pdu, "ERROR", 1, "Internal error"), socket);
				return VIGOR_REACTOR_CONTINUE;
			}

			fprintf(io, "---\n");
			fprintf(io, "# generated by bolo\n");

			char *name; state_t *state;
			for_each_key_value(&kernel->server->db.states, name, state) {
				fprintf(io, "%s:\n", name);
				fprintf(io, "  status:    %s\n", statstr(state->status));
				fprintf(io, "  message:   %s\n", state->summary);
				fprintf(io, "  last_seen: %i\n", state->last_seen);
				fprintf(io, "  fresh:     %s\n", state->stale ? "no" : "yes");
			}

			fflush(io);
			int fd = fileno(io);
			long off = lseek(fd, 0, SEEK_END);
			lseek(fd, 0, SEEK_SET);

			char *data = mmap(NULL, off, PROT_READ, MAP_PRIVATE, fd, 0);
			if (data == MAP_FAILED) {
				//pdu_send_and_free(pdu_reply(pdu, "ERROR", 1, "Internal error"), socket);
				pdu_send_and_free(pdu_reply(pdu, "ERROR", 1, strerror(errno)), socket);

			} else {
				pdu_send_and_free(pdu_reply(pdu, "DUMP", 1, data), socket);
			}

			fclose(io);
			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ GET.KEYS | name+ ] {{{ */
		if (_pdu_is(pdu, "GET.KEYS", 2, 0)) {
			pdu_t *a = pdu_reply(pdu, "VALUES", 0);
			int i; char *key, *value;

			for (i = 1; i < pdu_size(pdu); i++) {
				key = pdu_string(pdu, i);
				value = hash_get(&kernel->server->keys, key);
				if (value) {
					pdu_extendf(a, "%s", key);
					pdu_extendf(a, "%s", value);
				}
				free(key);
			}

			pdu_send_and_free(a, socket);
			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ DEL.KEYS | name+ ] {{{ */
		if (_pdu_is(pdu, "DEL.KEYS", 2, 0)) {
			int i;
			char *key;
			for (i = 1; i < pdu_size(pdu); i++) {
				key = pdu_string(pdu, i);
				logger(LOG_INFO, "deleting key %s", key);
				free(hash_set(&kernel->server->keys, key, NULL));
				free(key);
			}
			pdu_send_and_free(pdu_reply(pdu, "OK", 0), socket);
			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ SEARCH.KEYS | pattern ] {{{ */
		if (_pdu_is(pdu, "SEARCH.KEYS", 2, 2)) {
			char *pattern;
			const char *re_err;
			int re_off;
			pcre *re;

			pattern = pdu_string(pdu, 1);
			re = pcre_compile(pattern, 0, &re_err, &re_off, NULL);

			if (!re) {
				pdu_send_and_free(pdu_reply(pdu, "ERROR", 1, re_err), socket);

			} else {
				pdu_t *a = pdu_reply(pdu, "KEYS", 0);
				pcre_extra *re_extra = pcre_study(re, 0, &re_err);

				char *key, *value;
				for_each_key_value(&kernel->server->keys, key, value) {
					logger(LOG_DEBUG, "checking key '%s' against m/%s/", key, pattern);
					if (pcre_exec(re, re_extra, key, strlen(key), 0, 0, NULL, 0) != 0)
						continue;

					logger(LOG_INFO, "key '%s' matched m/%s/, adding to reply PDU", key, pattern);
					pdu_extendf(a, "%s", key);
				}

				pdu_send_and_free(a, socket);
				pcre_free_study(re_extra);
				pcre_free(re);
			}
			free(pattern);
			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ GET.EVENTS | since ] {{{ */
		if (_pdu_is(pdu, "GET.EVENTS", 2, 2)) {
			char *s = pdu_string(pdu, 1); int32_t since = strtol(s, NULL, 10); free(s);

			FILE *io = tmpfile();
			if (!io) {
				logger(LOG_ERR, "kernel cannot dump events; unable to create temporary file: %s", strerror(errno));
				pdu_send_and_free(pdu_reply(pdu, "ERROR", 1, "Internal error"), socket);
				return VIGOR_REACTOR_CONTINUE;
			}

			fprintf(io, "---\n");
			fprintf(io, "# generated by bolo\n");

			event_t *ev;
			for_each_object(ev, &kernel->server->db.events, l) {
				if (ev->timestamp < since)
					continue;

				fprintf(io, "- name:  %s\n", ev->name);
				fprintf(io, "  when:  %i\n", ev->timestamp);
				fprintf(io, "  extra: %s\n", ev->extra);
			}

			fflush(io);
			int fd = fileno(io);
			long off = lseek(fd, 0, SEEK_END);
			lseek(fd, 0, SEEK_SET);

			char *data = mmap(NULL, off, PROT_READ, MAP_PRIVATE, fd, 0);
			if (data == MAP_FAILED) {
				pdu_send_and_free(pdu_reply(pdu, "ERROR", 1, "Internal error"), socket);

			} else {
				pdu_send_and_free(pdu_reply(pdu, "EVENTS", 1, data), socket);
			}

			fclose(io);
			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ SAVESTATE ] {{{ */
		if (_pdu_is(pdu, "SAVESTATE", 1, 1)) {
			binf_write(&kernel->server->db, kernel->server->config.savefile);
			save_keys(&kernel->server->keys, kernel->server->config.keysfile);

			pdu_send_and_free(pdu_reply(pdu, "OK", 0), socket);
			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */

		logger(LOG_WARNING, "unhandled [%s] PDU (of %i frames) received on management port",
			pdu_type(pdu), pdu_size(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == kernel->listener) {
		/* [ STATE | ts | name | code | message ] {{{ */
		if (_pdu_is(pdu, "STATE", 5, 5)) {
			char *s;

			s = pdu_string(pdu, 1); uint32_t ts   = strtoul(s, NULL, 10); free(s);
			s = pdu_string(pdu, 3); uint8_t  code = atoi(s); free(s);
			char *name = pdu_string(pdu, 2);
			char *msg  = pdu_string(pdu, 4);

			if (name && *name && msg && *msg) {
				state_t *state = find_state(&kernel->server->db, name);
				if (state) {
					logger(LOG_INFO, "updating state %s, status=%i, ts=%i, msg=[%s]", name, code, ts, msg);
					int transition = state->stale || state->status != code;

					free(state->summary);
					state->status    = code;
					state->summary   = strdup(msg);
					state->last_seen = ts;
					state->expiry    = ts + state->type->freshness;
					state->stale     = 0;

					if (transition)
						broadcast_transition(kernel, state);
					broadcast_state(kernel, state);

				} else {
					logger(LOG_INFO, "ignoring update for unknown state %s, status=%i, ts=%i, msg=[%s]", name, code, ts, msg);
				}

			} else {
				logger(LOG_WARNING, "received malformed [STATE] PDU (no %s)",
					(!name || !*name ? "name" : "message"));
			}

			free(name);
			free(msg);

			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ COUNTER | ts | name | increment ] {{{ */
		if (_pdu_is(pdu, "COUNTER", 4, 4)) {
			char *s;
			s = pdu_string(pdu, 1); uint32_t ts   = strtoul(s, NULL, 10); free(s);
			s = pdu_string(pdu, 3);  int32_t incr = strtol (s, NULL, 10); free(s);
			char *name = pdu_string(pdu, 2);

			if (name && *name) {
				counter_t *counter = find_counter(&kernel->server->db, name);
				if (counter) {
					/* check for window closure */
					if (counter->last_seen > 0 && counter->last_seen != ts
					 && winstart(counter, counter->last_seen) != winstart(counter, ts)) {
						logger(LOG_INFO, "counter window rollover detected between %i and %i",
							winstart(counter, counter->last_seen), ts);
						broadcast_counter(kernel, counter);
						counter_reset(counter);
					}

					logger(LOG_INFO, "updating counter %s, ts=%i, incr=%i", name, ts, incr);
					counter->last_seen = ts;
					counter->value += incr;

				} else {
					logger(LOG_WARNING, "ignoring update for unknown counter %s, ts=%i, incr=%i", name, ts, incr);
				}
			} else {
				logger(LOG_WARNING, "received malformed [COUNTER] PDU (no name)");
			}
			free(name);

			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ SAMPLE | ts | name | value+ ] {{{ */
		if (_pdu_is(pdu, "SAMPLE", 4, 0)) {
			char *s;
			s = pdu_string(pdu, 1); uint32_t ts = strtoul(s, NULL, 10); free(s);
			char *name = pdu_string(pdu, 2);

			if (name && *name) {
				sample_t *sample = find_sample(&kernel->server->db, name);

				if (sample) {
					/* check for window closure */
					if (sample->last_seen > 0 && sample->last_seen != ts
					 && winstart(sample, sample->last_seen) != winstart(sample, ts)) {
						logger(LOG_INFO, "sample window rollover detected between %i and %i",
							winstart(sample, sample->last_seen), ts);
						broadcast_sample(kernel, sample);
						sample_reset(sample);
					}

					int i;
					for (i = 3; i < pdu_size(pdu); i++) {
						s = pdu_string(pdu, i); double v = strtod(s, NULL); free(s);

						logger(LOG_INFO, "%s sample set %s, ts=%i, value=%e", (sample->last_seen ? "updating" : "starting"), name, ts, v);
						if (sample_data(sample, v) != 0) {
							logger(LOG_ERR, "failed to update sample set %s, ts=%i, value=%e", name, ts, v);
							continue;
						}

						sample->last_seen = ts;
					}
				} else {
					logger(LOG_WARNING, "ignoring update for unknown sample set %s, ts=%i", name, ts);
				}
			} else {
				logger(LOG_WARNING, "received malformed [SAMPLE] PDU (no name)");
			}
			free(name);

			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ RATE | ts | name | value ] {{{ */
		if (_pdu_is(pdu, "RATE", 4, 4)) {
			char *s;
			s = pdu_string(pdu, 1); uint32_t ts = strtoul (s, NULL, 10); free(s);
			s = pdu_string(pdu, 3); uint64_t v  = strtoull(s, NULL, 10); free(s);
			char *name = pdu_string(pdu, 2);

			if (name && *name) {
				rate_t *rate = find_rate(&kernel->server->db, name);
				if (rate) {
					/* check for window closure */
					if (rate->last_seen > 0 && rate->last_seen != ts
					 && winstart(rate, rate->last_seen) != winstart(rate, ts)) {
						logger(LOG_INFO, "rate window rollover detected between %i and %i",
							winstart(rate, rate->last_seen), ts);
						broadcast_rate(kernel, rate);
						rate_reset(rate);
					}

					if (!rate->first_seen)
						rate->first_seen = ts;
					logger(LOG_INFO, "%s rate set %s, ts=%i, value=%lu", (rate->last_seen ? "updating" : "starting"), name, ts, v);
					if (rate_data(rate, v) != 0) {
						logger(LOG_ERR, "failed to update rate set %s, ts=%i, value=%lu", name, ts, v);
					} else {
						rate->last_seen = ts;
					}

				} else {
					logger(LOG_WARNING, "ignoring update for unknown rate set %s, ts=%i, value=%lu", name, ts, v);
				}
			} else {
				logger(LOG_WARNING, "received malformed [RATE] PDU (no name)");
			}
			free(name);

			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ EVENT | ts | name | description ] {{{ */
		if (_pdu_is(pdu, "EVENT", 4, 4)) {
			event_t *ev = vmalloc(sizeof(event_t));

			char *s = pdu_string(pdu, 1); ev->timestamp = strtol(s, NULL, 10); free(s);
			ev->name  = pdu_string(pdu, 2);
			ev->extra = pdu_string(pdu, 3);
			broadcast_event(kernel, ev);

			buffer_event(&kernel->server->db, ev,
				kernel->server->config.events_max,
				kernel->server->config.events_keep);

			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */
		/* [ SET.KEYS | (key|value)+ ] {{{ */
		if (_pdu_is(pdu, "SET.KEYS", 3, 0) && (pdu_size(pdu) - 1) % 2 == 0) {
			char *key, *value;
			int i;
			for (i = 1; i < pdu_size(pdu); i += 2) {
				key   = pdu_string(pdu, i);
				value = pdu_string(pdu, i + 1);

				logger(LOG_INFO, "set key %s = '%s'", key, value);
				char *existing = hash_set(&kernel->server->keys, key, value);
				free(key);
				if (existing != value)
					free(existing);
			}

			return VIGOR_REACTOR_CONTINUE;
		}
		/* }}} */

		logger(LOG_WARNING, "unhandled [%s] PDU (of %i frames) received on listener port",
			pdu_type(pdu), pdu_size(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
int core_kernel_thread(void *zmq, server_t *server) /* {{{ */
{
	assert(zmq != NULL);

	int rc;

	kernel_t *kernel = vmalloc(sizeof(kernel_t));
	kernel->server = server;

	if (kernel->server->config.savefile) {
		if (binf_read(&kernel->server->db, kernel->server->config.savefile) != 0) {
			logger(LOG_WARNING, "kernel failed to read state from %s: %s",
					kernel->server->config.savefile, strerror(errno));
		}
	}

	if (kernel->server->config.keysfile) {
		if (read_keys(&kernel->server->keys, kernel->server->config.keysfile) != 0) {
			logger(LOG_WARNING, "kernel failed to read keys from %s: %s",
					kernel->server->config.keysfile, strerror(errno));
		}
	}

	logger(LOG_DEBUG, "kernel: connecting kernel.control to supervisor.command");
	rc = core_connect_supervisor(zmq, &kernel->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG,"kernel: connecting kernel.tock to scheduler.tick");
	rc = core_connect_scheduler(zmq, &kernel->tock);
	if (rc != 0)
		return rc;

	if (server->config.listener) {
		logger(LOG_DEBUG, "kernel: binding kernel.listener PULL socket to %s",
			server->config.listener);
		kernel->listener = zmq_socket(zmq, ZMQ_PULL);
		if (!kernel->listener)
			return -1;
		rc = zmq_bind(kernel->listener, server->config.listener);
		if (rc != 0)
			return rc;
	} else {
		logger(LOG_DEBUG, "kernel: no listener bind specified; skipping");
	}

	if (server->config.broadcast) {
		logger(LOG_DEBUG, "kernel: binding kernel.broadcast PUB socket to %s",
			server->config.broadcast);
		kernel->broadcast = zmq_socket(zmq, ZMQ_PUB);
		if (!kernel->broadcast)
			return -1;
		rc = zmq_bind(kernel->broadcast, server->config.broadcast);
		if (rc != 0)
			return rc;
	} else {
		logger(LOG_DEBUG, "kernel: no broadcast bind specified; skipping");
	}

	if (server->config.controller) {
		logger(LOG_DEBUG, "kernel: binding kernel.management ROUTER socket to %s",
			server->config.controller);
		kernel->management = zmq_socket(zmq, ZMQ_ROUTER);
		if (!kernel->management)
			return -1;
		rc = zmq_bind(kernel->management, server->config.controller);
		if (rc != 0)
			return rc;
	} else {
		logger(LOG_DEBUG, "kernel: no management bind specified; skipping");
	}

	logger(LOG_DEBUG, "kernel: setting up event reactor");
	kernel->reactor = reactor_new();
	if (!kernel->reactor)
		return -1;

	logger(LOG_DEBUG, "kernel: registering kernel.control with event reactor");
	rc = reactor_set(kernel->reactor, kernel->control, _kernel_reactor, kernel);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "kernel: registering kernel.tock with event reactor");
	rc = reactor_set(kernel->reactor, kernel->tock, _kernel_reactor, kernel);
	if (rc != 0)
		return rc;

	if (kernel->listener) {
		logger(LOG_DEBUG, "kernel: registering kernel.listener with event reactor");
		rc = reactor_set(kernel->reactor, kernel->listener, _kernel_reactor, kernel);
		if (rc != 0)
			return rc;
	}

	if (kernel->management) {
		logger(LOG_DEBUG, "kernel: registering kernel.management with event reactor");
		rc = reactor_set(kernel->reactor, kernel->management, _kernel_reactor, kernel);
		if (rc != 0)
			return rc;
	}

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _kernel_thread, kernel);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

int core_supervisor(void *zmq) /* {{{ */
{
	int rc, sig;
	void *command;

	logger(LOG_DEBUG, "binding PUB socket supervisor.command");
	command = zmq_socket(zmq, ZMQ_PUB);
	if (!command)
		return -1;
	rc = zmq_bind(command, "inproc://bolo/v1/supervisor.command");
	if (rc != 0)
		return rc;

	sigset_t signals;
	sigemptyset(&signals);
	sigaddset(&signals, SIGTERM);
	sigaddset(&signals, SIGINT);

	rc = pthread_sigmask(SIG_UNBLOCK, &signals, NULL);
	if (rc != 0)
		return rc;

	for (;;) {
		rc = sigwait(&signals, &sig);
		if (rc != 0) {
			logger(LOG_ERR, "sigwait: %s", strerror(errno));
			sig = SIGTERM; /* let's just pretend */
		}

		if (sig == SIGTERM || sig == SIGINT) {
			logger(LOG_INFO, "supervisor caught SIG%s; shutting down",
				sig == SIGTERM ? "TERM" : "INT");

			pdu_send_and_free(pdu_make("TERMINATE", 0), command);
			break;
		}

		logger(LOG_ERR, "ignoring unexpected signal %i\n", sig);
	}

	zmq_close(command);
	zmq_ctx_destroy(zmq);
	logger(LOG_DEBUG, "supervisor: terminated");
	return 0;
}
/* }}} */
