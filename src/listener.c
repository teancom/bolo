#include "bolo.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

void* listener(void *u)
{
	server_t *s = (server_t*)u;
	if (!s) {
		logger(LOG_CRIT, "listener failed: server context was NULL");
		return NULL;
	}

	void *db = zmq_socket(s->zmq, ZMQ_DEALER);
	if (!db) {
		logger(LOG_CRIT, "listener failed to get a DEALER socket");
		return NULL;
	}
	if (zmq_connect(db, DB_MANAGER_ENDPOINT) != 0) {
		logger(LOG_CRIT, "listener failed to connect to db manager at " DB_MANAGER_ENDPOINT);
		return NULL;
	}

	void *z = zmq_socket(s->zmq, ZMQ_ROUTER);
	if (!z) {
		logger(LOG_CRIT, "listener failed to get a ROUTER socket");
		return NULL;
	}
	if (zmq_bind(z, s->config.listener) != 0) {
		logger(LOG_CRIT, "listener failed to bind to %s",
			s->config.listener);
		return NULL;
	}

	pdu_t *q, *a;
	while ((q = pdu_recv(z)) != NULL) {
		if (!pdu_type(q)) {
			logger(LOG_ERR, "listener received an empty PDU; ignoring");
			continue;
		}

		if (strcmp(pdu_type(q), "SUBMIT") == 0) {
			char *ts   = string("%u", time_s());
			char *name = pdu_string(q, 1);
			char *code = pdu_string(q, 2);
			char *msg  = pdu_string(q, 3);

			pdu_send_and_free(pdu_make("UPDATE", 4, ts, name, code, msg), db);
			a = pdu_reply(q, "OK", 0);

			free(ts);
			free(name);
			free(code);
			free(msg);

		} else {
			a = pdu_reply(q, "ERROR", 1, "Invalid PDU");
		}

		pdu_free(q);
		pdu_send_and_free(a, z);
	}

	return NULL;
}
