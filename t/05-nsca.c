#include "test.h"

TESTS {
	mkdir("t/tmp", 0755);

	alarm(5);
	server_t svr;
	void *kernel;
	pthread_t tid;
	struct sockaddr_in sa;

	svr.config.nsca_port = 3232;
	CHECK(svr.zmq = zmq_ctx_new(),
		"failed to create a new 0MQ context");
	CHECK(kernel = zmq_socket(svr.zmq, ZMQ_ROUTER),
		"failed to create mock kernel test socket");
	CHECK(zmq_bind(kernel, KERNEL_ENDPOINT) == 0,
		"failed to bind to kernel socket");
	CHECK(pthread_create(&tid, NULL, nsca_gateway, &svr) == 0,
		"failed to spin up NSCA gateway thread");
	sleep_ms(150);

	sa.sin_family = AF_INET;
	sa.sin_port   = htons(3232);
	CHECK(inet_pton(sa.sin_family, "127.0.0.1", &sa.sin_addr.s_addr) == 1,
		"failed to convert '127.0.0.1' into a numeric IP address...");

	/* ----------------------------- */

	pdu_t *q, *a;
	char nsca[4300], *p;
	int fd;

	/* a normal, full write */
	/* v=1, ts=1419549963, status=1, crc32=2700527054 */
	memset(nsca, 0, 4300); p = nsca;
	memcpy(p, "\0\x01\xa0\xf6\xc5\xceT\x9c\x9d\x0b\0\x01", 12); p +=  12;
	memcpy(p, "host.fq.dn", 10);                                p +=  64;
	memcpy(p, "service_name", 12);                              p += 128;
	memcpy(p, "WARNING: service is not that great", 34);

	CHECK((fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0,
		"failed to get a client socket descriptor");
	CHECK(connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0,
		"failed to connect to 127.0.0.1:3232 over TCP");

	CHECK(write(fd, nsca, 4300) == 4300,
		"failed to write all 4300 bytes of NSCA packet to socket");
	close(fd);

		/* receive a PUT.STATE at the kernel */
		q = pdu_recv(kernel);
		is(pdu_type(q), "PUT.STATE", "NSCA gateway sent a PUT.STATE");
		is(pdu_string(q, 1), "1419549963", "relayed event timestamp");
		is(pdu_string(q, 2), "host.fq.dn:service_name",
			"constructed state name from host and service");
		is(pdu_string(q, 3), "1", "relayed status code");
		is(pdu_string(q, 4), "WARNING: service is not that great",
			"relayed summary mesage");

		a = pdu_reply(q, "OK", 0);
		is_int(pdu_send(a, kernel), 0, "kernel sent [OK] reply");

	/* short write */
	/* v=1, ts=1419550502, status=0, crc32=440361285 */
	memset(nsca, 0, 4300); p = nsca;
	memcpy(p, "\0\x01\x1a?aET\x9c\x9f&\0\0", 12); p +=  12;
	memcpy(p, "short", 5);                        p +=  64;
	memcpy(p, "write", 5);                        p += 128;
	memcpy(p, "a short write", 13);

	CHECK((fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0,
		"failed to get a client socket descriptor");
	CHECK(connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0,
		"failed to connect to 127.0.0.1:3232 over TCP");

	CHECK(write(fd, nsca, 4299) == 4299,
		"failed to write first 4299 bytes of NSCA packet to socket");
	close(fd);

	/* no write, just open the socket and then close it */
	CHECK((fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0,
		"failed to get a client socket descriptor");
	CHECK(connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0,
		"failed to connect to 127.0.0.1:3232 over TCP");
	close(fd);

	/* another full (to verify that the short write didn't go through */
	/* v=1, ts=1419550842, status=0, crc32=500643599 */
	memset(nsca, 0, 4300); p = nsca;
	memcpy(p, "\0\x01\x1d\xd7\7\x0fT\x9c\xa0z\0\0", 12); p +=  12;
	memcpy(p, "final", 5);                               p +=  64;
	memcpy(p, "state", 5);                               p += 128;
	memcpy(p, "thats all folks", 15);

	CHECK((fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0,
		"failed to get a client socket descriptor");
	CHECK(connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0,
		"failed to connect to 127.0.0.1:3232 over TCP");

	CHECK(write(fd, nsca, 4300) == 4300,
		"failed to write all 4300 bytes of NSCA packet to socket");
	close(fd);

		/* receive a PUT.STATE at the kernel */
		q = pdu_recv(kernel);
		is(pdu_type(q), "PUT.STATE", "NSCA gateway sent a PUT.STATE");
		is(pdu_string(q, 1), "1419550842",      "final:state timestamp");
		is(pdu_string(q, 2), "final:state",     "final:state name");
		is(pdu_string(q, 3), "0",               "final:state status");
		is(pdu_string(q, 4), "thats all folks", "final:state summary");

		a = pdu_reply(q, "OK", 0);
		is_int(pdu_send(a, kernel), 0, "kernel sent [OK] reply");

	/* test perfdata processing */
	/* v=1, ts=1419551063, status=0, crc32=496396109 */
	memset(nsca, 0, 4300); p = nsca;
	memcpy(p, "\0\x01\x1d\x96gMT\x9c\xa1W\0\0", 12); p +=  12;
	memcpy(p, "perf", 4);                            p +=  64;
	memcpy(p, "data", 4);                            p += 128;
	memcpy(p, "good | perf1=42;; perf2=43;;;; perf4=0.123456789;0;16;14;15", 59);

	CHECK((fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0,
		"failed to get a client socket descriptor");
	CHECK(connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0,
		"failed to connect to 127.0.0.1:3232 over TCP");

	CHECK(write(fd, nsca, 4300) == 4300,
		"failed to write all 4300 bytes of NSCA packet to socket");
	close(fd);

		/* receive a PUT.STATE at the kernel */
		q = pdu_recv(kernel);
		is(pdu_type(q), "PUT.STATE", "NSCA gateway sent a PUT.STATE");
		is(pdu_string(q, 1), "1419551063", "perf:data timestamp");
		is(pdu_string(q, 2), "perf:data",  "perf:data name");
		is(pdu_string(q, 3), "0",          "perf:data status");
		is(pdu_string(q, 4), "good",       "perf:data summary");

		a = pdu_reply(q, "OK", 0);
		is_int(pdu_send(a, kernel), 0, "kernel sent [OK] reply");

		/* receive a PUT.SAMPLE at the kernel (perf1) */
		q = pdu_recv(kernel);
		is(pdu_type(q), "PUT.SAMPLE", "NSCA gateway sent a PUT.SAMPLE");
		is(pdu_string(q, 1), "1419551063",       "perf:data:perf1 timestamp");
		is(pdu_string(q, 2), "perf:data:perf1",  "perf:data:perf1 name");
		is(pdu_string(q, 3), "42",               "perf:data:perf1 value");

		a = pdu_reply(q, "OK", 0);
		is_int(pdu_send(a, kernel), 0, "kernel sent [OK] reply");

		/* receive a PUT.SAMPLE at the kernel (perf2) */
		q = pdu_recv(kernel);
		is(pdu_type(q), "PUT.SAMPLE", "NSCA gateway sent a PUT.SAMPLE");
		is(pdu_string(q, 1), "1419551063",       "perf:data:perf2 timestamp");
		is(pdu_string(q, 2), "perf:data:perf2",  "perf:data:perf2 name");
		is(pdu_string(q, 3), "43",               "perf:data:perf2 value");

		a = pdu_reply(q, "OK", 0);
		is_int(pdu_send(a, kernel), 0, "kernel sent [OK] reply");

		/* receive a PUT.SAMPLE at the kernel (perf4) */
		q = pdu_recv(kernel);
		is(pdu_type(q), "PUT.SAMPLE", "NSCA gateway sent a PUT.SAMPLE");
		is(pdu_string(q, 1), "1419551063",       "perf:data:perf4 timestamp");
		is(pdu_string(q, 2), "perf:data:perf4",  "perf:data:perf4 name");
		is(pdu_string(q, 3), "0.123456789",      "perf:data:perf4 value");

		a = pdu_reply(q, "OK", 0);
		is_int(pdu_send(a, kernel), 0, "kernel sent [OK] reply");

	/* ----------------------------- */
	pthread_cancel(tid);
	pthread_join(tid, NULL);
}
