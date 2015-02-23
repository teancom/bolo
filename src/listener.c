#include "bolo.h"

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
		logger(LOG_INFO, "listener cleaning up; closing kernel client socket");
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
	if (zmq_connect(l->client, KERNEL_ENDPOINT) != 0) {
		logger(LOG_CRIT, "listener failed to connect to kernel at " KERNEL_ENDPOINT);
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

		if (strcmp(pdu_type(q), "STATE") == 0 && pdu_size(q) == 5) {
			char *ts   = pdu_string(q, 1);
			char *name = pdu_string(q, 2);
			char *code = pdu_string(q, 3);
			char *msg  = pdu_string(q, 4);

			pdu_send_and_free(pdu_make("PUT.STATE", 4, ts, name, code, msg), l->client);
			pdu_t *p = pdu_recv(l->client); pdu_free(p);
			a = pdu_reply(q, "OK", 0);

			free(ts);
			free(name);
			free(code);
			free(msg);

		} else if (strcmp(pdu_type(q), "COUNTER") == 0 && (pdu_size(q) == 3 || pdu_size(q) == 4)) {
			char *ts   = pdu_string(q, 1);
			char *name = pdu_string(q, 2);
			char *incr = pdu_string(q, 3);
			if (!incr) incr = strdup("1");

			pdu_send_and_free(pdu_make("PUT.COUNTER", 3, ts, name, incr), l->client);
			pdu_t *p = pdu_recv(l->client); pdu_free(p);
			a = pdu_reply(q, "OK", 0);

			free(ts);
			free(name);
			free(incr);

		} else if (strcmp(pdu_type(q), "SAMPLE") == 0 && pdu_size(q) > 3) {
			size_t i;

			char *err  = NULL;
			char *ts   = pdu_string(q, 1);
			char *name = pdu_string(q, 2);
			for (i = 3; i < pdu_size(q); i++) {
				char *val  = pdu_string(q, i);
				pdu_send_and_free(pdu_make("PUT.SAMPLE", 3, ts, name, val), l->client);
				pdu_t *p = pdu_recv(l->client);
				if (strcmp(pdu_type(p), "ERROR") == 0) {
					free(err);
					err = pdu_string(p, 1);
				}
				pdu_free(p);
				free(val);
			}
			if (err) {
				a = pdu_reply(q, "ERROR", 1, err);
				free(err);
			} else {
				a = pdu_reply(q, "OK", 0);
			}

			free(ts);
			free(name);

		} else if (strcmp(pdu_type(q), "SET.KEYS") == 0 && pdu_size(q) > 2 && (pdu_size(q) - 1) % 2 == 0) {
			pdu_send_and_free(vx_pdu_dup(q, NULL), l->client);
			pdu_t *p = pdu_recv(l->client);
			if (strcmp(pdu_type(p), "ERROR") == 0) {
				char *err = pdu_string(p, 1);
				a = pdu_reply(q, "ERROR", 1, err);
				free(err);
			} else {
				a = pdu_reply(q, "OK", 0);
			}
			pdu_free(p);

		} else {
			logger(LOG_WARNING, "listener received an invalid [%s] PDU, of %i frames",
				pdu_type(q), pdu_size(q));
			a = pdu_reply(q, "ERROR", 1, "Invalid PDU");
		}

		pdu_free(q);
		pdu_send_and_free(a, l->listener);
	}

	pthread_cleanup_pop(1);
	return NULL;
}
