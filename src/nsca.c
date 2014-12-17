#include "bolo.h"
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#define EPOLL_MAX_FD 64

#define CLIENT_MAX    16384
#define CLIENT_EXPIRE 600

#define NSCA_MAX_HOSTNAME 64
#define NSCA_MAX_SERVICE  128
#define NSCA_MAX_OUTPUT   4096
#define NSCA_PACKET_LEN   12 + 64 + 128 + 4096

typedef struct {
	int    fd;
	size_t bytes;
	struct {
		uint32_t  crc32;
		uint32_t  timestamp;
		uint16_t  version;
		uint16_t  status;
		char      host[NSCA_MAX_HOSTNAME];
		char      service[NSCA_MAX_SERVICE];
		char      output[NSCA_MAX_OUTPUT];
	} packet;
} client_t;

static client_t* client_new(int fd)
{
	client_t *c = calloc(1, sizeof(client_t));
	c->fd = fd;
	return c;
}

static void client_free(void *_c)
{
	client_t *c = (client_t*)_c;
	close(c->fd);
	free(c);
}

static void die(const char *m)
{
	perror(m);
	abort();
}

static void inline fdflags(int fd, int flags)
{
	int orig = fcntl(fd, F_GETFL, 0);
	if (orig < 0 || fcntl(fd, F_SETFL, orig|flags) < 0)
		die("fcntl");
}

void* nsca_listener(void *u)
{
	struct epoll_event ev, events[EPOLL_MAX_FD];
	int n, nfds, epfd, connfd;
	socklen_t peerlen;
	struct sockaddr_in peer;
	server_t *s;
	void *db;

	s = (server_t*)u;
	assert(s);
	assert(s->nsca_socket >= 0);
	fdflags(s->nsca_socket, O_NONBLOCK);

	db = zmq_socket(s->zmq, ZMQ_DEALER);
	assert(db);
	zmq_connect(db, DB_MANAGER_ENDPOINT);

	epfd = epoll_create1(0);
	assert(epfd >= 0);

	ev.events = EPOLLIN;
	ev.data.fd = s->nsca_socket;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, s->nsca_socket, &ev) != 0)
		return NULL;

	cache_t *clients = cache_new(CLIENT_MAX, CLIENT_EXPIRE);
	cache_setopt(clients, VIGOR_CACHE_DESTRUCTOR, client_free);

	for (;;) {
		nfds = epoll_wait(epfd, events, EPOLL_MAX_FD, -1);
		if (nfds == -1)
			return NULL;

		for (n = 0; n < nfds; n++) {
			char *id = string("%04x", events[n].data.fd);
			client_t *c;

			if (events[n].data.fd == s->nsca_socket) {
				/* new inbound connection */
				connfd = accept(s->nsca_socket, (struct sockaddr*)&peer, &peerlen);
				if (connfd < 0) die("accept");
				fdflags(connfd, O_NONBLOCK);

				ev.events = EPOLLIN | EPOLLET;
				ev.data.fd = connfd;
				if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) != 0)
					return NULL;

				/* push new client state into cache */
				c = client_new(connfd);
				if (!cache_set(clients, id, c))
					client_free(c);

			} else {
				c = cache_get(clients, id);
				if (!c) die("cache_get");

				/* read what's left from the client */
				size_t n = read(c->fd, &c->packet+c->bytes, NSCA_PACKET_LEN - c->bytes);
				if (n > 0) {
					c->bytes += n;

					if (c->bytes == NSCA_PACKET_LEN) {
						client_free(c);
						pdu_t *q = pdu_make("UPDATE", 0);
						pdu_extendf(q, "%u", ntohl(c->packet.timestamp));
						pdu_extendf(q, "%s:%s", c->packet.host, c->packet.service);
						pdu_extendf(q, "%u", ntohs(c->packet.status) & 0xff);
						pdu_extendf(q, "%s", c->packet.output);
						pdu_send(q, db);

						pdu_t *a = pdu_recv(db);
						assert(strcmp(pdu_type(a), "OK") == 0);
					}
				} else {
					client_free(cache_unset(clients, id));
				}
			}
		}
	}

	return NULL;
}
