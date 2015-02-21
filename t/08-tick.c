#include "test.h"

TESTS {
	mkdir("t/tmp", 0755);

	alarm(5);
	server_t svr; memset(&svr, 0, sizeof(svr));

	write_file("t/tmp/config",
		"# sample test config\n"
		"broadcast inproc://bcast\n"
		"dumpfiles t/tmp/dump.\%s\n"
		"savefile  t/tmp/save\n"
		"window @default 2\n"
		"sample  @default sample1\n"
		"counter @default counter1\n"
		"", 0);
	CHECK(configure("t/tmp/config", &svr) == 0,
		"failed to configure bolo");
	/* there is no savefile */
	unlink(svr.config.savefile);

	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");

	pthread_t tid;
	CHECK(pthread_create(&tid, NULL, kernel, &svr) == 0,
		"failed to spin up kernel thread");
	sleep_ms(50);

	void *kernel;
	CHECK(kernel = zmq_socket(svr.zmq, ZMQ_DEALER),
		"failed to create mock kernel test socket");
	CHECK(zmq_connect(kernel, KERNEL_ENDPOINT) == 0,
		"failed to connect to kernel socket");

	void *bcast;
	CHECK(bcast = zmq_socket(svr.zmq, ZMQ_SUB),
		"failed to create broadcast subscriber tocket");
	CHECK(zmq_setsockopt(bcast, ZMQ_SUBSCRIBE, "", 0) == 0,
		"failed to set subscriber socket option");
	CHECK(zmq_connect(bcast, "inproc://bcast") == 0,
		"failed to connect to broadcast socket");
	sleep_ms(150);

	/* ----------------------------- */

	pdu_t *q, *a;

	/* first, the samples */
	q = pdu_make("PUT.SAMPLE", 3,
		"1000", /* ts; arbitrary */
		"sample1", "10");
	is_int(pdu_send_and_free(q, kernel), 0, "sent 1st PUT.SAMPLE for t1000 to kernel");
	CHECK(a = pdu_recv(kernel), "failed to get reply from kernel");
	is(pdu_type(a), "OK", "got [OK] from kernel");

	q = pdu_make("PUT.SAMPLE", 3,
		"1000", /* same time frame */
		"sample1", "10");
	is_int(pdu_send_and_free(q, kernel), 0, "sent 2nd PUT.SAMPLE for t1000 to kernel");
	CHECK(a = pdu_recv(kernel), "failed to get reply from kernel");
	is(pdu_type(a), "OK", "got [OK] from kernel");

	q = pdu_make("PUT.SAMPLE", 3,
		"1001", /* still in the t1000 2s window */
		"sample1", "10");
	is_int(pdu_send_and_free(q, kernel), 0, "sent PUT.SAMPLE for t1001 to kernel");
	CHECK(a = pdu_recv(kernel), "failed to get reply from kernel");
	is(pdu_type(a), "OK", "got [OK] from kernel");

	/* then, the counter */
	q = pdu_make("PUT.COUNTER", 3,
			"2000", /* arbitrary timestamp */
			"counter1", "1"); /* counter1 == 1 */
	is_int(pdu_send_and_free(q, kernel), 0, "sent 1st PUT.COUNTER for t2000 to kernel");
	CHECK(a = pdu_recv(kernel), "failed to get reply from kernel");
	is(pdu_type(a), "OK", "got [OK] from kernel");

	q = pdu_make("PUT.COUNTER", 3,
			"2000", /* arbitrary timestamp */
			"counter1", "2"); /* counter1 == 3 */
	is_int(pdu_send_and_free(q, kernel), 0, "sent 2nd PUT.COUNTER for t2000 to kernel");
	CHECK(a = pdu_recv(kernel), "failed to get reply from kernel");
	is(pdu_type(a), "OK", "got [OK] from kernel");

	q = pdu_make("PUT.COUNTER", 3,
			"2000", /* arbitrary timestamp */
			"counter1", "5"); /* counter1 == 8 */
	is_int(pdu_send_and_free(q, kernel), 0, "sent 3rd PUT.COUNTER for t2000 to kernel");
	CHECK(a = pdu_recv(kernel), "failed to get reply from kernel");
	is(pdu_type(a), "OK", "got [OK] from kernel");

	q = pdu_make("PUT.COUNTER", 3,
			"2001", /* still in the t2000 time window */
			"counter1", "1"); /* counter1 == 9 */
	is_int(pdu_send_and_free(q, kernel), 0, "sent PUT.COUNTER for t2001 to kernel");
	CHECK(a = pdu_recv(kernel), "failed to get reply from kernel");
	is(pdu_type(a), "OK", "got [OK] from kernel");

	/* tick tock */
	q = pdu_make("TICK", 0);
	is_int(pdu_send_and_free(q, kernel), 0, "sent TICK for NOW");
	CHECK(a = pdu_recv(kernel), "failed to get reply from kernel");
	is(pdu_type(a), "OK", "got [OK] from kernel");

	/* await the broadcast */
	CHECK(a = pdu_recv(bcast), "failed to get a broadcast from kernel");
	is(pdu_type(a), "COUNTER", "got [COUNTER] broadcast from kernel");
	is(pdu_string(a, 1), "2000",     "COUNTER[0] is window start timestamp");
	is(pdu_string(a, 2), "counter1", "COUNTER[1] is counter name");
	is(pdu_string(a, 3), "9",        "COUNTER[2] is counter value");
	is_null(pdu_string(a, 4), "there is no COUNTER[3]");

	/* await the broadcast */
	CHECK(a = pdu_recv(bcast), "failed to get a broadcast from kernel");
	is(pdu_type(a), "SAMPLE", "got [SAMPLE] broadcast from kernel");
	is(pdu_string(a, 1), "1000",    "SAMPLE[0] is window start timestamp");
	is(pdu_string(a, 2), "sample1", "SAMPLE[1] is sample set name");
	is(pdu_string(a, 3), "3",       "SAMPLE[2] is number of samples");
	is(pdu_string(a, 4), "1.000000e+01", "SAMPLE[3] is minimum value");
	is(pdu_string(a, 5), "1.000000e+01", "SAMPLE[4] is maximum value");
	is(pdu_string(a, 6), "3.000000e+01", "SAMPLE[5] is summation value");
	is(pdu_string(a, 7), "1.000000e+01", "SAMPLE[6] is mean value");
	is(pdu_string(a, 8), "0.000000e+00", "SAMPLE[7] is set variance");
	is_null(pdu_string(a, 9), "there is no SAMPLE[8]");

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);
	zmq_close(kernel);
	zmq_close(bcast);
}
