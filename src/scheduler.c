#include "bolo.h"
#include <unistd.h>
#include <pthread.h>

typedef struct {
	server_t *server;
	void     *client;
} scheduler_t;

static void* cleanup_scheduler(void *_)
{
	scheduler_t *s = (scheduler_t*)_;

	if (s->client) {
		logger(LOG_INFO, "scheduler cleaning up; closing db manager client socket");
		vzmq_shutdown(s->client, 0);
	}

	free(s);
	return NULL;
}

void* scheduler(void *u)
{
	scheduler_t *s = calloc(1, sizeof(scheduler_t));
	pthread_cleanup_push(cleanup_scheduler, s);

	s->server = (server_t*)u;
	if (!s->server) {
		logger(LOG_CRIT, "scheduler failed: server context was NULL");
		return NULL;
	}

	if (!s->server->interval.tick)
		s->server->interval.tick = 1000;

	s->client = zmq_socket(s->server->zmq, ZMQ_DEALER);
	if (!s->client) {
		logger(LOG_CRIT, "scheduler failed to get a DEALER socket");
		return NULL;
	}

	if (zmq_connect(s->client, DB_MANAGER_ENDPOINT) != 0) {
		logger(LOG_CRIT, "scheduler failed to connect to db manager at " DB_MANAGER_ENDPOINT);
		return NULL;
	}

	pdu_t *a;
	uint16_t freshness = s->server->interval.freshness;
	uint16_t savestate = s->server->interval.savestate;
	for (;;) {
		if (freshness == 0) {
			pdu_send_and_free(pdu_make("CHECKFRESH", 0), s->client);
			a = pdu_recv(s->client);
			pdu_free(a);
			freshness = s->server->interval.freshness;
		} else {
			freshness--;
		}

		if (savestate == 0) {
			pdu_send_and_free(pdu_make("SAVESTATE", 0), s->client);
			a = pdu_recv(s->client);
			pdu_free(a);
			savestate = s->server->interval.savestate;
		} else {
			savestate--;
		}
		sleep_ms(s->server->interval.tick);
	}

	pthread_cleanup_pop(1);
	return NULL;
}
