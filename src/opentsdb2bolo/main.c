/*
  Copyright (c) 2016 The Bolo Authors.  All Rights Reserved.

  This file is part of Bolo.

  Bolo is free software: you can redistribute it and/or modify it under the
  terms of the GNU General Public License as published by the Free Software
  Foundation, either version 3 of the License, or (at your option) any later
  version.

  Bolo is distributed in the hope that it will be useful, but WITHOUT ANY
  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along
  with Bolo.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "../bolo.h"
#include <getopt.h>
#include <sys/epoll.h>

#define ME "opentsdb2bolo"

typedef struct {
	int     fd;
	char    buffer[8192];
	size_t  nread;
} client_t;

static int s_listen(const char *bindto, int backlog)
{
	struct sockaddr_in addr;
	char *buf, *p;
	int fd, rc, v, port = 13321;

	buf = strdup(bindto);
	if (!buf) {
		errno = ENOBUFS;
		return -1;
	}

	p = strchr(buf, ':');
	if (p) {
		*p++ = '\0';
		port = atoi(p);
	}
	if (port < 0 || port > 65535) {
		errno = EINVAL;
		free(buf);
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port);

	if (strcmp(buf, "*") == 0) {
		free(buf);
		addr.sin_addr.s_addr = INADDR_ANY;
	} else {
		rc = inet_pton(addr.sin_family, buf, &addr.sin_addr);
		free(buf);
		if (rc) return -1;
	}

	fd = socket(addr.sin_family, SOCK_STREAM, 0);
	if (fd < 0) return fd;

	v = 1;
	rc = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
	if (rc != 0) {
		logger(LOG_WARNING, "failed to set SO_REUSEADDR when binding to %s: %s (error %d) -- skipping...", bindto, strerror(errno), errno);
	}

	rc = bind(fd, (struct sockaddr*)&addr, sizeof(addr));
	if (rc) {
		close(fd);
		return -1;
	}

	rc = listen(fd, backlog);
	if (rc) {
		close(fd);
		return -1;
	}

	return fd;
}

static int s_send(char *buf, void *zocket)
{
	/* [put system.load.1m 1471535210 0.27 deployment=vault-warden index=1 job=vault]
	              ^            ^       ^           ^                  ^        ^
	              |            |       |           |                  |        |
	              |            |       |           `------------------'--------'---- tags
	              |            |       |
	              |            |       `---- value
	              |            |
	              |            `----  timestamp
	              |
	              `---- metric name
	
	   we want to take and re-order as:
	      [sample <timestamp> <tag1>,<tag2>,<tagn>,metric=<metric> <value>]
	 */
	int rc;
	char *a, *b, *name, *ts, *value;

	if (memcmp(buf, "put ", 4) != 0) return -1;

	for (a = buf + 4; *a && !isspace(*a); a++);
	for (           ; *a &&  isspace(*a); a++);
	for (b = a      ; *b && !isspace(*b); b++);
	/* a...b is the timestamp */
	ts = calloc(sizeof(char), b - a + 1);
	if (!ts) return -1;
	memcpy(ts, a, b - a);

	for (a = b; *a &&  isspace(*a); a++);
	for (b = a; *b && !isspace(*b); b++);
	/* a...b is the value */
	value = calloc(sizeof(char), b - a + 1);
	if (!value) return -1;
	memcpy(value, a, b - a);

	for (a = b; *a && isspace(*a); a++);
	b = name = calloc(strlen(buf) + 32, sizeof(char));
	if (!b) return -1;
	/* tags first */
	while (*a) {
		for (; *a && !isspace(*a); *b++ = *a++);
		*b++ = ',';
		for (; *a &&  isspace(*a); a++);
	}
	*b++ = 'm';
	*b++ = 'e';
	*b++ = 't';
	*b++ = 'r';
	*b++ = 'i';
	*b++ = 'c';
	*b++ = '=';
	for (a = buf + 4; *a && !isspace(*a); *b++ = *a++);
	*b++ = '\0';

	logger(LOG_DEBUG, "sending SAMPLE %s %s %s", ts, name, value);
	rc = pdu_send_and_free(pdu_make("SAMPLE", 3, ts, name, value), zocket);
	if (rc != 0) {
		logger(LOG_WARNING, "Failed to send [SAMPLE|%s|%s|%s] PDU to upstream bolo",
				ts, name, value);
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	void *zmq, *pipe;
	int listenfd, acceptfd, rc, i;
	client_t *clients;
	struct epoll_event ev, events[32];

	struct {
		char *submit_to;
		char *listen;

		int   verbose;
		int   daemonize;

		char *pidfile;
		char *user;
		char *group;

		char *prefix;
		int   maxconn;
	} OPTIONS = {
		.submit_to = strdup("tcp://127.0.0.1:2999"),
		.listen    = strdup("*:13321"),
		.verbose   = 0,
		.daemonize = 1,
		.pidfile   = strdup("/var/run/" ME ".pid"),
		.user      = strdup("root"),
		.group     = strdup("root"),
		.prefix    = NULL, /* will be set later, if needed */
		.maxconn   = 1024,
	};

	struct option long_opts[] = {
		{ "help",                  no_argument, NULL, 'h' },
		{ "version",               no_argument, NULL, 'V' },
		{ "verbose",               no_argument, NULL, 'v' },
		{ "listen",          required_argument, NULL, 'l' },
		{ "submit",          required_argument, NULL, 'S' },
		{ "foreground",            no_argument, NULL, 'F' },
		{ "pidfile",         required_argument, NULL, 'p' },
		{ "user",            required_argument, NULL, 'u' },
		{ "group",           required_argument, NULL, 'g' },
		{ "prefix",          required_argument, NULL, 'P' },
		{ "max-connections", required_argument, NULL, 'm' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+l:S:Fp:u:g:P:m:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf(ME " v%s\n", BOLO_VERSION);
			printf("Usage: " ME " [-h?FVv] [-l ip:port] [-P PREFIX] [-S tcp://host:port]\n"
			       "                [-u user] [-g group] [-p /path/to/pidfile]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -F, --foreground     don't daemonize, stay in the foreground\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -l, --listen         ip:port to bind and listen for opentsdb data\n");
			printf("  -S, --submit         bolo listener to relay metrics to\n");
			printf("  -P, --prefix         optional prefix to prepend to all metrics\n");
			printf("  -u, --user           user to run as (if daemonized)\n");
			printf("  -g, --group          group to run as (if daemonized)\n");
			printf("  -p, --pidfile        where to store the pidfile (if daemonized)\n");
			exit(0);

		case 'V':
			printf(ME " v%s\n"
			       "Copyright (c) 2016 The Bolo Authors.  All Rights Reserved.\n",
			       BOLO_VERSION);
			exit(0);

		case 'v':
			OPTIONS.verbose++;
			break;

		case 'F':
			OPTIONS.daemonize = 0;
			break;

		case 'p':
			free(OPTIONS.pidfile);
			OPTIONS.pidfile = strdup(optarg);
			break;

		case 'u':
			free(OPTIONS.user);
			OPTIONS.user = strdup(optarg);
			break;

		case 'g':
			free(OPTIONS.group);
			OPTIONS.group = strdup(optarg);
			break;

		case 'l':
			free(OPTIONS.listen);
			OPTIONS.listen = strdup(optarg);
			break;

		case 'S':
			free(OPTIONS.submit_to);
			OPTIONS.submit_to = strdup(optarg);
			break;

		case 'P':
			free(OPTIONS.prefix);
			OPTIONS.prefix = strdup(optarg);
			break;

		case 'm':
			OPTIONS.maxconn = strtoll(optarg, NULL, 10);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			exit(1);
		}
	}

	if (OPTIONS.daemonize) {
		log_open(ME, "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			exit(3);
		}
		umask(um);
	} else {
		log_open(ME, "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}

	logger(LOG_DEBUG, "allocating buffers for up to %d connections", OPTIONS.maxconn);
	clients = calloc(OPTIONS.maxconn, sizeof(client_t));
	if (!clients) {
		logger(LOG_ERR, "failed to allocate buffers for up to %d connections: %s (error %d)", OPTIONS.maxconn, strerror(errno), errno);
		return 1;
	}
	for (i = 0; i < OPTIONS.maxconn; i++) {
		clients[i].fd = -1;
	}

	logger(LOG_DEBUG, "connecting to bolo aggregator at %s", OPTIONS.submit_to);
	zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		return 1;
	}
	pipe = zmq_socket(zmq, ZMQ_PUSH);
	if (!pipe) {
		logger(LOG_ERR, "failed to create a PUSH socket");
		return 1;
	}
	if (vzmq_connect(pipe, OPTIONS.submit_to) != 0) {
		logger(LOG_ERR, "failed to connect to %s", OPTIONS.submit_to);
		return 1;
	}

	logger(LOG_DEBUG, "binding %s to listen for inbound opentsdb submissions", OPTIONS.listen);
	listenfd = s_listen(OPTIONS.listen, 2048);
	if (listenfd < 0) {
		logger(LOG_ERR, "failed to listen on %s: %s (error %d)", OPTIONS.listen, strerror(errno), errno);
		return 1;
	}

	logger(LOG_DEBUG, "initializing epoll multiplexer");
	int epfd = epoll_create(OPTIONS.maxconn + 1);
	if (epfd < 0) {
		logger(LOG_ERR, "failed to create epoll fd: %s (error %d)", strerror(errno), errno);
		return 1;
	}

	logger(LOG_DEBUG, "registering socket fd with epoll");
	ev.events = EPOLLIN;
	ev.data.fd = listenfd;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) == -1) {
		logger(LOG_ERR, "failed to add listener fd to epoll: %s (error %d)", strerror(errno), errno);
		return 1;
	}

	logger(LOG_DEBUG, "entering main event loop");
	for (;;) {
		int j, nfds;

		nfds = epoll_wait(epfd, events, 32, -1);
		if (nfds == -1) {
			perror("epoll_pwait");
			exit(EXIT_FAILURE);
		}

		logger(LOG_DEBUG, "got %d active file descriptors from epoll", nfds);
		for (i = 0; i < nfds; i++) {
			struct sockaddr_in peer;
			socklen_t peer_len = sizeof(peer);
			char peer_ip[INET_ADDRSTRLEN];
			int peer_port = 0;

			logger(LOG_DEBUG, "fd %d is active, per epoll", events[i].data.fd);

			if (events[i].data.fd == listenfd) { /* accept()'able */
				for (j = 0; j < OPTIONS.maxconn; j++)
					if (clients[j].fd == -1)
						break;

				acceptfd = accept(listenfd, (struct sockaddr*)&peer, &peer_len);
				if (acceptfd < 0) {
					logger(LOG_ERR, "error accepting inbound connection: %s (error %d)", strerror(errno), errno);
					continue;
				}
				peer_port = ntohs(peer.sin_port);
				if (inet_ntop(peer.sin_family, &peer.sin_addr, peer_ip, sizeof(peer_ip) / sizeof(peer_ip[0])) == NULL) {
					logger(LOG_WARNING, "unable to format remote peer ip as a string: %s (error %d)", strerror(errno), errno);
					memcpy(peer_ip, "<unknown>", 10);
				}

				if (j >= OPTIONS.maxconn) {
					logger(LOG_INFO, "max connections (%d) reached; ignoring inbound connection attempt from %s:%d", OPTIONS.maxconn, peer_ip, peer_port);
					close(acceptfd);
					continue;
				}

				logger(LOG_INFO, "inbound connection from %s:%d", peer_ip, peer_port);
				logger(LOG_DEBUG, "adding fd %d for connection from %s:%d to epoll multiplexer", acceptfd, peer_ip, peer_port);
				ev.events = EPOLLIN;
				ev.data.fd = acceptfd;
				rc = epoll_ctl(epfd, EPOLL_CTL_ADD, ev.data.fd, &ev);
				if (rc == -1) {
					logger(LOG_ERR, "failed to add new connection epoll multiplexer: %s (error %d)", strerror(errno), errno);
					close(acceptfd);
					continue;
				}

				clients[j].fd = acceptfd;
				clients[j].nread = 0;
			}

			for (j = 0; j < OPTIONS.maxconn; j++) {
				ssize_t n;
				char *eol;
				int eof = 0;

				if (clients[j].fd != events[i].data.fd) {
					continue;
				}

				logger(LOG_DEBUG, "reading data for client %d fd %d", j, clients[j].fd);
				n = read(clients[j].fd, clients[j].buffer, 8190 - clients[j].nread - 1);
				if (n < 0) {
					logger(LOG_ERR, "error reading from client %d fd %d: %s (error %d)", j, clients[j].fd, strerror(errno), errno);
					return 1;
				}
				if (n == 0) {
					eof = 1;
					if (clients[j].nread) {
						clients[j].buffer[clients[j].nread++] = '\n';
					}
				}

				/* look for a newline */
				*(clients[j].buffer + clients[j].nread + n) = '\0';
				eol = strchr(clients[j].buffer + clients[j].nread, '\n');
				clients[j].nread += n;

				while (eol) {
					*eol++ = '\0';
					logger(LOG_DEBUG, "detected opentsdb pdu [%s]", clients[j].buffer);

					s_send(clients[j].buffer, pipe);

					n = eol - clients[j].buffer;
					clients[j].nread -= n;
					memmove(clients[j].buffer, eol, clients[j].nread);
					eol = strchr(clients[j].buffer, '\n');
				}

				if (eof) {
					logger(LOG_DEBUG, "EOF reached, closing connection to client %d fd %d", j, clients[j].fd);
					close(clients[j].fd);
					clients[j].fd = -1;
				}
				break;
			}
		}
	}

	return 0;
}
