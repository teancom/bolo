#include "bolo.h"
#include <sys/mman.h>

typedef struct {
	server_t *server;
	void     *listener;
	void     *client;
} controller_t;

static void* cleanup_controller(void *_)
{
	controller_t *c = (controller_t*)_;

	if (c->listener) {
		logger(LOG_INFO, "controller cleaning up; closing listening socket");
		vzmq_shutdown(c->listener, 500);
	}
	if (c->client) {
		logger(LOG_INFO, "controller cleaning up; closing kernel client socket");
		vzmq_shutdown(c->client, 0);
	}

	free(c);
	return NULL;
}

void* controller(void *u)
{
	controller_t *c = calloc(1, sizeof(controller_t));
	pthread_cleanup_push(cleanup_controller, c);

	c->server = (server_t*)u;
	if (!c->server) {
		logger(LOG_CRIT, "controller failed: server context was NULL");
		return NULL;
	}

	c->client = zmq_socket(c->server->zmq, ZMQ_DEALER);
	if (!c->client) {
		logger(LOG_CRIT, "controller failed to get a DEALER socket");
		return NULL;
	}
	if (zmq_connect(c->client, KERNEL_ENDPOINT) != 0) {
		logger(LOG_CRIT, "controller failed to connect to kernel at " KERNEL_ENDPOINT);
		return NULL;
	}

	c->listener = zmq_socket(c->server->zmq, ZMQ_ROUTER);
	if (!c->listener) {
		logger(LOG_CRIT, "controller failed to get a ROUTER socket to bind");
		return NULL;
	}
	if (zmq_bind(c->listener, c->server->config.controller) != 0) {
		logger(LOG_CRIT, "controller failed to bind to %s", c->server->config.controller);
		return  NULL;
	}

	seed_randomness();

	pdu_t *q, *a, *res;
	char *s;
	int rc;

	while ((q = pdu_recv(c->listener)) != NULL) {
		if (strcmp(pdu_type(q), "STATE") == 0) {
			rc = pdu_send_and_free(pdu_make("GET.STATE", 1,
				s = pdu_string(q, 1)), c->client); free(s);
			if (rc != 0) {
				a = pdu_reply(q, "ERROR", 1, "Internal Error");

			} else {
				res = pdu_recv(c->client);
				if (!res) {
					a = pdu_reply(q, "ERROR", 1, "Internal Error");
				} else if (strcmp(pdu_type(res), "ERROR") == 0) {
					a = pdu_reply(q, "ERROR", 1, s = pdu_string(res, 1)); free(s);
				} else if (strcmp(pdu_type(res), "STATE") == 0) {
					char *name, *ts, *stale, *code, *msg;
					a = pdu_reply(q, pdu_type(res), 5,
						name  = pdu_string(res, 1),
						ts    = pdu_string(res, 2),
						stale = pdu_string(res, 3),
						code  = pdu_string(res, 4),
						msg   = pdu_string(res, 5));
					fprintf(stderr, "[STATE|%s|%s|%s|%s|%s] sent\n", name, ts, stale, code, msg);
					free(name);
					free(ts);
					free(stale);
					free(code);
					free(msg);
				} else {
					a = pdu_reply(q, "ERROR", 1, "Internal Error");
				}
			}

		} else if (strcmp(pdu_type(q), "DUMP") == 0) {
			rc = pdu_send_and_free(pdu_make("DUMP", 1,
				s = string("%04x%04x", rand(), rand())), c->client); free(s);
			if (rc != 0) {
				a = pdu_reply(q, "ERROR", 1, "Internal Error");

			} else {
				res = pdu_recv(c->client);

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
			}

		} else {
			a = pdu_reply(q, "ERROR", 1, "Invalid PDU");
		}

		pdu_send_and_free(a, c->listener);
		pdu_free(q);
	}

	pthread_cleanup_pop(1);
	return NULL;
}
