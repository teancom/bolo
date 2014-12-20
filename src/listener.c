#include "bolo.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <pthread.h>

typedef struct {
	server_t *server;
	void     *listener;
	void     *client;
} listener_t;

static void* cleanup_listener(void *_)
{
	listener_t *l = (listener_t*)_;

	if (l->listener) {
		logger(LOG_INFO, "listener cleaning up; closing listening socket");
		vzmq_shutdown(l->listener, 500);
	}
	if (l->client) {
		logger(LOG_INFO, "listener cleaning up; closing db manager client socket");
		vzmq_shutdown(l->client, 0);
	}

	free(l);
	return NULL;
}

void* listener(void *u)
{
	listener_t *l = calloc(1, sizeof(listener_t));
	pthread_cleanup_push(cleanup_listener, l);

	l->server = (server_t*)u;
	if (!l->server) {
		logger(LOG_CRIT, "listener failed: server context was NULL");
		return NULL;
	}

	l->client = zmq_socket(l->server->zmq, ZMQ_DEALER);
	if (!l->client) {
		logger(LOG_CRIT, "listener failed to get a DEALER socket");
		return NULL;
	}
	if (zmq_connect(l->client, DB_MANAGER_ENDPOINT) != 0) {
		logger(LOG_CRIT, "listener failed to connect to db manager at " DB_MANAGER_ENDPOINT);
		return NULL;
	}

	l->listener = zmq_socket(l->server->zmq, ZMQ_ROUTER);
	if (!l->listener) {
		logger(LOG_CRIT, "listener failed to get a ROUTER socket");
		return NULL;
	}
	if (zmq_bind(l->listener, l->server->config.listener) != 0) {
		logger(LOG_CRIT, "listener failed to bind to %s",
			l->server->config.listener);
		return NULL;
	}

	pdu_t *q, *a;
	while ((q = pdu_recv(l->listener)) != NULL) {
		if (!pdu_type(q)) {
			logger(LOG_ERR, "listener received an empty PDU; ignoring");
			continue;
		}

		if (strcmp(pdu_type(q), "SUBMIT") == 0) {
			char *ts   = string("%u", time_s());
			char *name = pdu_string(q, 1);
			char *code = pdu_string(q, 2);
			char *msg  = pdu_string(q, 3);

			pdu_send_and_free(pdu_make("UPDATE", 4, ts, name, code, msg), l->client);
			a = pdu_reply(q, "OK", 0);

			free(ts);
			free(name);
			free(code);
			free(msg);

		} else {
			a = pdu_reply(q, "ERROR", 1, "Invalid PDU");
		}

		pdu_free(q);
		pdu_send_and_free(a, l->listener);
	}

	pthread_cleanup_pop(1);
	return NULL;
}
