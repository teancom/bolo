#include "test.h"

TESTS {
	mkdir("t/tmp", 0755);

	alarm(5);
	server_t svr;
	void *z;
	pthread_t tid;

	svr.interval.tick      = 10; /* ms */
	svr.interval.freshness = 1;
	svr.interval.savestate = 3;

	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(z = zmq_socket(svr.zmq, ZMQ_ROUTER),
		"failed to create mock kernel test socket");
	CHECK(zmq_bind(z, KERNEL_ENDPOINT) == 0,
		"failed to connect to kernel socket");
	CHECK(pthread_create(&tid, NULL, scheduler, &svr) == 0,
		"failed to spin up scheduler thread");

	/* ----------------------------- */

	size_t checkfresh = 0;
	size_t savestate  = 0;

	alarm(20);
	for (;;) {
		pdu_t *q, *a;

		q = pdu_recv(z);
		isnt_null(q, "received PDU from scheduler");
		if (strcmp(pdu_type(q), "SAVESTATE") == 0) {
			savestate++;
		} else if (strcmp(pdu_type(q), "CHECKFRESH") == 0) {
			checkfresh++;
		} else {
			diag("received a [%s] PDU", pdu_type(q));
			BAIL_OUT("Unepxected PDU from scheduler");
		}

		a = pdu_reply(q, "OK", 0);
		pdu_send_and_free(a, z);
		pdu_free(q);

		if (checkfresh + savestate > 10)
			break;
	}

	ok(checkfresh >= 6, "received at least two [CHECKFRESH]");
	ok(savestate  >= 2, "received at least four [SAVESTATE]");

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);
	zmq_close(z);
}
