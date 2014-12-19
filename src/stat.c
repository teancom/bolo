#include "bolo.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>

void* stat_listener(void *u)
{
	int rc;
	server_t *svr = (server_t*)u;

	void *z = zmq_socket(svr->zmq, ZMQ_ROUTER);
	assert(z);
	rc = zmq_bind(z, svr->config.stat_endpoint);
	assert(rc == 0);

	void *dbman = zmq_socket(svr->zmq, ZMQ_DEALER);
	assert(dbman);
	rc = zmq_connect(dbman, DB_MANAGER_ENDPOINT);
	assert(rc == 0);

	pdu_t *q, *a, *res;
	char *s;

	while ((q = pdu_recv(z)) != NULL) {
		if (strcmp(pdu_type(q), "STATE") == 0) {
			rc = pdu_send_and_free(pdu_make("STATE", 1,
				s = pdu_string(q, 1)), dbman); free(s);
			assert(rc == 0);
			res = pdu_recv(dbman);
			a = pdu_reply(q, pdu_type(res), 1,
				s = pdu_string(res, 1)); free(s);

		} else if (strcmp(pdu_type(q), "DUMP") == 0) {
			seed_randomness();
			rc = pdu_send_and_free(pdu_make("DUMP", 1,
				s = string("%04x%04x", rand(), rand())), dbman); free(s);
			assert(rc == 0);
			res = pdu_recv(dbman);

			if (strcmp(pdu_type(res), "ERROR") == 0) {
				a = pdu_reply(q, "ERROR", 1, s = pdu_string(res, 1)); free(s);

			} else {
				s = pdu_string(res, 1);
				int fd = open(s = pdu_string(res, 1), O_RDONLY);
				unlink(s); free(s);

				long off = lseek(fd, 0, SEEK_END);
				lseek(fd, 0, SEEK_SET);

				char *data = mmap(NULL, off, PROT_READ, MAP_PRIVATE, fd, 0);
				if (data == MAP_FAILED) {
					a = pdu_reply(q, "ERROR", 1, "Internal Error");
				} else {
					a = pdu_reply(q, "DUMP", 1, data);
				}
				close(fd);
			}

		} else {
			a = pdu_reply(q, "ERROR", 1, "Invalid PDU");
		}

		pdu_send_and_free(a, z);
		pdu_free(q);
	}

	return NULL; /* LCOV_EXCL_LINE */
}
