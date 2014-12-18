#include "bolo.h"
#include <assert.h>
#include <unistd.h>

void* scheduler(void *u)
{
	server_t *s = (server_t*)u;

	void *db = zmq_socket(s->zmq, ZMQ_DEALER);
	assert(db);
	int rc = zmq_connect(db, DB_MANAGER_ENDPOINT);
	assert(rc == 0);

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
