#include "bolo.h"
#include <unistd.h>

void* scheduler(void *u)
{
	server_t *s = (server_t*)u;

	void *db = zmq_socket(s->zmq, ZMQ_DEALER);
	if (!db) {
		logger(LOG_CRIT, "scheduler failed to get a DEALER socket");
		return NULL;
	}

	if (zmq_connect(db, DB_MANAGER_ENDPOINT) != 0) {
		logger(LOG_CRIT, "scheduler failed to connect to db manager at " DB_MANAGER_ENDPOINT);
		return NULL;
	}

	if (!s->interval.tick)
		s->interval.tick = 1000;

	pdu_t *a;
	uint16_t freshness = s->interval.freshness;
	uint16_t savestate = s->interval.savestate;
	for (;;) {
		if (freshness == 0) {
			pdu_send_and_free(pdu_make("CHECKFRESH", 0), db);
			a = pdu_recv(db);
			pdu_free(a);
			freshness = s->interval.freshness;
		} else {
			freshness--;
		}

		if (savestate == 0) {
			pdu_send_and_free(pdu_make("SAVESTATE", 0), db);
			a = pdu_recv(db);
			pdu_free(a);
			savestate = s->interval.savestate;
		} else {
			savestate--;
		}
		sleep_ms(s->interval.tick);
	}
}
