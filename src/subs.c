#include "bolo.h"
#include "subs.h"
#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <vigor.h>

#define METRIC_TYPE_COUNT  1
#define METRIC_TYPE_SAMPLE 2

#define MAX_SAMPLES       256

typedef struct {
	int type;

	uint64_t  count;

	size_t    nsamples;
	double    samples[MAX_SAMPLES];
} metric_t;

typedef struct {
	void *zmq;
	void *command;    /* PUB:  command socket for controlling all other actors */
} supervisor_t;

typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */

	void *tick;       /* PUB:  broadcasts timing interrupts */
	int   interval;
} scheduler_t;

typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *tock;       /* SUB:  hooked up to scheduler.tick, for timing interrupts */

	void *input;      /* PULL: receives metric submission requests from other actors */
	void *submission; /* PUSH: connected to bolo submission port (external) */

	reactor_t *reactor;

	char *prefix;

	hash_t metrics;
} monitor_t;

/***********************************************************/

int subscriber_init(void) /* {{{ */
{
	int rc;
	sigset_t blocked;
	sigfillset(&blocked);

	rc = pthread_sigmask(SIG_BLOCK, &blocked, NULL);
	if (rc != 0)
		return rc;

	return rc;
}
/* }}} */

/***********************************************************/

static int _cmp(const void *a, const void *b) /* {{{ */
{
	return *(double*)a == *(double*)b ? 0 :
	       *(double*)a >  *(double*)b ? 1 : -1;
}
/* }}} */
static double _median(metric_t *metric) /* {{{ */
{
	if (metric->type != METRIC_TYPE_SAMPLE)
		return 0.0;

	int n = metric->nsamples;
	if (n == 0)
		return 0.0;

	if (n > MAX_SAMPLES)
		n = MAX_SAMPLES;

	qsort(metric->samples, n, sizeof(double), _cmp);
	int mid = n / 2;

	if (n % 2 == 0)
		return (metric->samples[mid - 1] + metric->samples[mid]) / 2.;
	else
		return metric->samples[mid];
}
/* }}} */
static void _reset(metric_t *metric) /* {{{ */
{
	if (!metric) return;
	metric->count = 0;
	metric->nsamples = 0;
	memset(metric->samples, 0, sizeof(double) * MAX_SAMPLES);
}
/* }}} */
static metric_t* _metric(monitor_t *monitor, const char *name, int type) /* {{{ */
{
	metric_t *metric = hash_get(&monitor->metrics, name);
	if (!metric) {
		metric = vmalloc(sizeof(metric_t));
		metric->type = type;
		_reset(metric);
		hash_set(&monitor->metrics, name, metric);
	}

	if (metric->type != type)
		return NULL;

	return metric;
}
/* }}} */
static int _monitor_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	monitor_t *monitor = (monitor_t*)_;

	/* FIXME: any message from supervisor.control == exit! */
	if (socket == monitor->control)
		return VIGOR_REACTOR_HALT;

	if (socket == monitor->tock) {
		logger(LOG_DEBUG, "submitting collected metrics");
		char *name;
		metric_t *value;
		double median;

		for_each_key_value(&monitor->metrics, name, value) {
			char *submit = string("%s:%s", monitor->prefix, name);
			switch (value->type) {
			case METRIC_TYPE_COUNT:
				logger(LOG_DEBUG, "METRIC %s => [%lu]", submit, value->count);
				pdu_send_and_free(
					counter_pdu(submit, value->count),
					monitor->submission);
				_reset(value);
				break;

			case METRIC_TYPE_SAMPLE:
				median = _median(value);
				logger(LOG_DEBUG, "METRIC %s => [%lf]", submit, median);
				pdu_send_and_free(
					sample_pdu(submit, 1, median),
					monitor->submission);
				_reset(value);
				break;

			default:
				break;
			}
			free(submit);
		}

		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == monitor->input) {
		if (strcmp(pdu_type(pdu), "INIT") == 0 && pdu_size(pdu) == 3) {
			char *type = pdu_string(pdu, 1);
			char *name = pdu_string(pdu, 2);

			if (strcmp(type, "SAMPLE") == 0)
				_metric(monitor, name, METRIC_TYPE_SAMPLE);
			else if (strcmp(type, "COUNT") == 0)
				_metric(monitor, name, METRIC_TYPE_COUNT);

			free(name);
			free(type);


		} else if (strcmp(pdu_type(pdu), "SAMPLE") == 0 && pdu_size(pdu) == 3) {
			char * name  = pdu_string(pdu, 1);
			char *_value = pdu_string(pdu, 2);
			char *error;

			double value = strtod(_value, &error);
			if (!*error) {
				metric_t *metric = _metric(monitor, name, METRIC_TYPE_SAMPLE);
				if (metric) {
					if (metric->nsamples < MAX_SAMPLES) {
						metric->samples[metric->nsamples++] = value;

					} else {
						int i = rand() / RAND_MAX * ++metric->nsamples;
						if (i < MAX_SAMPLES)
							metric->samples[i] = value;
					}
				}
			}

			free(name);
			free(_value);


		} else if (strcmp(pdu_type(pdu), "COUNT") == 0 && pdu_size(pdu) >= 2) {
			char * name  = pdu_string(pdu, 1);
			char *_value = pdu_string(pdu, 2);
			char *error;

			uint64_t value;
			if (_value) {
				value = strtoull(_value, &error, 10);
			} else {
				value = 1;
				error = "";
			}

			if (!*error) {
				metric_t *metric = _metric(monitor, name, METRIC_TYPE_COUNT);
				if (metric) {
					metric->count += value;
				}
			}

			free(name);
			free(_value);
		}

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
static void * _monitor_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	monitor_t *monitor = (monitor_t*)_;

	reactor_go(monitor->reactor);
	logger(LOG_DEBUG, "monitor: shutting down");

	zmq_close(monitor->control);
	zmq_close(monitor->tock);
	zmq_close(monitor->input);
	vzmq_shutdown(monitor->submission, 0);

	reactor_free(monitor->reactor);

	free(monitor->prefix);

	char *name;
	metric_t *metric;
	for_each_key_value(&monitor->metrics, name, metric) {
		free(metric);
	}
	hash_done(&monitor->metrics, 0);

	free(monitor);

	logger(LOG_DEBUG, "monitor: terminated");
	return NULL;
}
/* }}} */
int subscriber_monitor_thread(void *zmq, const char *prefix, const char *endpoint) /* {{{ */
{
	assert(zmq != NULL);
	assert(prefix != NULL);
	assert(endpoint != NULL);

	int rc;

	monitor_t *monitor = vmalloc(sizeof(monitor_t));
	monitor->prefix = strdup(prefix);

	logger(LOG_DEBUG, "connecting monitor.control -> supervisor");
	rc = subscriber_connect_supervisor(zmq, &monitor->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting monitor.tock -> scheduler");
	rc = subscriber_connect_scheduler(zmq, &monitor->tock);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "binding PULL socket monitor.input");
	monitor->input = zmq_socket(zmq, ZMQ_PULL);
	if (!monitor->input)
		return -1;
	rc = zmq_bind(monitor->input, "inproc://bolo.sub/v1/monitor.input");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "connecting monitor.submission -> bolo at %s", endpoint);
	monitor->submission = zmq_socket(zmq, ZMQ_PUSH);
	if (!monitor->submission)
		return -1;
	rc = vzmq_connect_af(monitor->submission, endpoint, AF_UNSPEC);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "setting up monitor event reactor");
	monitor->reactor = reactor_new();
	if (!monitor->reactor)
		return -1;

	logger(LOG_DEBUG, "monitor: registering monitor.control with event reactor");
	rc = reactor_set(monitor->reactor, monitor->control, _monitor_reactor, monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "monitor: registering monitor.tock with event reactor");
	rc = reactor_set(monitor->reactor, monitor->tock, _monitor_reactor, monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "monitor: registering monitor.input with event reactor");
	rc = reactor_set(monitor->reactor, monitor->input, _monitor_reactor, monitor);
	if (rc != 0)
		return rc;

	pthread_t tid;
	pthread_create(&tid, NULL, _monitor_thread, monitor);

	return 0;
}
/* }}} */

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
int subscriber_scheduler_thread(void *zmq, int interval) /* {{{ */
{
	assert(zmq != NULL);
	assert(interval > 0);

	int rc;
	scheduler_t *scheduler = vmalloc(sizeof(scheduler_t));
	scheduler->interval = interval;

	logger(LOG_DEBUG, "connecting scheduler.control -> supervisor");
	rc = subscriber_connect_supervisor(zmq, &scheduler->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "binding PUB socket for scheduler.tick");
	scheduler->tick = zmq_socket(zmq, ZMQ_PUB);
	if (!scheduler->tick)
		return -1;
	rc = zmq_bind(scheduler->tick, "inproc://bolo.sub/v1/scheduler.tick");
	if (rc != 0)
		return rc;

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _scheduler_thread, scheduler);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

int subscriber_supervisor(void *zmq) /* {{{ */
{
	int rc, sig;
	void *command;

	logger(LOG_DEBUG, "binding PUB socket supervisor.command");
	command = zmq_socket(zmq, ZMQ_PUB);
	if (!command)
		return -1;
	rc = zmq_bind(command, "inproc://bolo.sub/v1/supervisor.command");
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

int subscriber_metrics(void *zmq, ...) /* {{{ */
{
	int rc;
	void *zocket;
	va_list ap;

	rc = subscriber_connect_monitor(zmq, &zocket);
	if (rc != 0)
		return rc;

	const char *type, *name;
	va_start(ap, zmq);
	for (;;) {
		type = va_arg(ap, const char *);
		if (type == NULL)
			break;

		name = va_arg(ap, const char *);
		if (name == NULL)
			break;

		if (pdu_send_and_free(pdu_make("INIT", 2, type, name), zocket) != 0)
			rc--;
	}
	va_end(ap);

	zmq_close(zocket);
	return rc;
}
/* }}} */

int subscriber_connect_monitor(void *zmq, void **zocket) /* {{{ */
{
	assert(zmq != NULL);
	assert(zocket != NULL);

	int rc;

	*zocket = zmq_socket(zmq, ZMQ_PUSH);
	if (!*zocket)
		return -1;

	rc = zmq_connect(*zocket, "inproc://bolo.sub/v1/monitor.input");
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
int subscriber_connect_scheduler(void *zmq, void **zocket) /* {{{ */
{
	assert(zmq != NULL);
	assert(zocket != NULL);

	int rc;

	*zocket = zmq_socket(zmq, ZMQ_SUB);
	if (!*zocket)
		return -1;

	rc = zmq_connect(*zocket, "inproc://bolo.sub/v1/scheduler.tick");
	if (rc != 0)
		return rc;

	rc = zmq_setsockopt(*zocket, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
int subscriber_connect_supervisor(void *zmq, void **zocket) /* {{{ */
{
	assert(zmq != NULL);
	assert(zocket != NULL);

	int rc;

	*zocket = zmq_socket(zmq, ZMQ_SUB);
	if (!*zocket)
		return -1;

	rc = zmq_connect(*zocket, "inproc://bolo.sub/v1/supervisor.command");
	if (rc != 0)
		return rc;

	rc = zmq_setsockopt(*zocket, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */
