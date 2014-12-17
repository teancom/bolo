#include "bolo.h"
#include <assert.h>
#include <string.h>

void* db_manager(void *u)
{
	int rc;
	server_t *s = (server_t*)u;

	void *db = zmq_socket(s->zmq, ZMQ_ROUTER);
	assert(db);
	rc = zmq_bind(db, DB_MANAGER_ENDPOINT);
	assert(rc == 0);

	pdu_t *q, *a;
	while ((q = pdu_recv(db)) != NULL) {
		if (strcmp(pdu_type(q), "UPDATE") == 0) {
			a = pdu_reply(q, "OK", 0);

		} else if (strcmp(pdu_type(q), "STATE") == 0) {
			a = pdu_reply(q, "ERROR", 1, "Not Implemented");

		} else if (strcmp(pdu_type(q), "DUMP") == 0) {
			a = pdu_reply(q, "ERROR", 1, "Not Implemented");

		} else if (strcmp(pdu_type(q), "CHECKFRESH") == 0) {
			a = pdu_reply(q, "ERROR", 1, "Not Implemented");

		} else if (strcmp(pdu_type(q), "SAVESTATE") == 0) {
			a = pdu_reply(q, "ERROR", 1, "Not Implemented");

		} else {
			a = pdu_reply(q, "ERROR", 1, "Invalid PDU");
		}

		pdu_send_and_free(a, db);
		pdu_free(q);
	}

	return NULL;
}
