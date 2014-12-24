#include "test.h"

TESTS {
	mkdir("t/tmp", 0755);

	alarm(5);
	server_t svr;
	void *kernel;
	pthread_t tid;
	int fd;
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

	CHECK((fd = socket(AF_INET, SOCK_STREAM, 0)) >= 0,
		"failed to get a client socket descriptor");
	sa.sin_family = AF_INET;
	sa.sin_port   = htons(3232);
	CHECK(inet_pton(sa.sin_family, "127.0.0.1", &sa.sin_addr.s_addr) == 1,
		"failed to convert '127.0.0.1' into a numeric IP address...");
	CHECK(connect(fd, (struct sockaddr*)&sa, sizeof(sa)) == 0,
		"failed to connect to 127.0.0.1:3232 over TCP");

	/* ----------------------------- */

	/* ----------------------------- */
	close(fd);
	pthread_cancel(tid);
	pthread_join(tid, NULL);
}
