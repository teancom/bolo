/*
  Copyright 2015 James Hunt <james@jameshunt.us>

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

#include "bolo.h"
#include <getopt.h>

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>

#define BOLO_EPOLL_MAXFD 8192
#define AGENT_TICK_MS 100

static struct {
	char *endpoint;
	char *commands;

	int   verbose;
	int   daemonize;

	char *pidfile;
	char *user;
	char *group;
} OPTIONS = { 0 };

typedef struct {
	list_t     l;
	int        interval;
	char      *exec;

	uint64_t   next_run;
	pid_t      pid;
	int        fd;           /* hooked up to stdout */

	size_t     nread;
	char       buffer[8192];
} command_t;

static int s_parse(list_t *list, const char *file)
{
	/* line format:
	   # comment
	   @60s /command/to/run every 60s
	   @1h  /path/to/hourly/command
	 */

	FILE *io = fopen(file, "r");
	if (!io) {
		logger(LOG_ERR, "failed to open commands file %s for reading: %s",
			file, strerror(errno));
		return 1;
	}

	int n = 0;
	char line[8192];
	while (fgets(line, 8192, io) != NULL) {
		n++;
		char *nl = strrchr(line, '\n');
		if (!nl) {
			logger(LOG_WARNING, "%s:%i too long; skipping", file, n);
			continue;
		}

		*nl = '\0';

		char *a = line;
		while (isspace(*a)) a++;

		if (!*a || *a == '#')
			continue;

		if (*a == '@') {
			a++;
			int x = 0;
			while (isdigit(*a)) {
				x = x * 10 + (*a - '0');
				a++;
			}

			if (x == 0) {
				logger(LOG_WARNING, "%s:%i has illegal time spec; skipping", file, n);
				continue;
			}

			switch (*a) {
			case 'h': x *= 3600; break;
			case 'm': x *= 60;   break;
			case 's':            break;
			default:
				logger(LOG_WARNING, "%s:%i has illegal time unit '%c'; skipping", file, n, *a);
				continue;
			}
			a++;

			if (x < 0) {
				logger(LOG_WARNING, "%s:%i time overflow detected; skipping", file, n);
				continue;
			}

			if (!isspace(*a)) {
				logger(LOG_WARNING, "%s:%i malformed; skipping", file, n);
				continue;
			}
			while (isspace(*a)) a++;

			command_t *cmd = vmalloc(sizeof(command_t));
			cmd->next_run =  0;
			cmd->pid      = -1;
			cmd->fd       = -1;
			cmd->nread    =  0;

			cmd->interval = x * 1000;
			cmd->exec = strdup(a);
			list_push(list, &cmd->l);
			continue;
		}

		logger(LOG_WARNING, "%s:%i malformed; skipping", file, n);
	}

	fclose(io);
	return 0;
}

static void s_send(void *zmq, const char *endpoint, const char *line)
{
	pdu_t *pdu = stream_pdu(line);
	if (!pdu) return;

	void *z = zmq_socket(zmq, ZMQ_PUSH);
	if (!z) return;

	int rc = zmq_connect(z, endpoint);
	if (rc != 0) return;

	pdu_send_and_free(pdu, z);
	zmq_close(z);
}

int main(int argc, char **argv)
{
	OPTIONS.verbose   = 0;
	OPTIONS.endpoint  = strdup("tcp://127.0.0.1:2997");
	OPTIONS.commands  = strdup(DEFAULT_AGENT_FILE);
	OPTIONS.daemonize = 1;
	OPTIONS.user      = strdup("root");
	OPTIONS.group     = strdup("root");

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "verbose",          no_argument, NULL, 'v' },
		{ "quiet",            no_argument, NULL, 'q' },
		{ "endpoint",   required_argument, NULL, 'e' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "commands",   required_argument, NULL, 'c' },
		{ "pidfile",    required_argument, NULL, 'p' },
		{ "user",       required_argument, NULL, 'u' },
		{ "group",      required_argument, NULL, 'g' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+qe:Fc:p:u:g:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			break;

		case 'v':
			OPTIONS.verbose++;
			break;

		case 'q':
			OPTIONS.verbose = 0;
			break;

		case 'e':
			free(OPTIONS.endpoint);
			OPTIONS.endpoint = strdup(optarg);
			break;

		case 'F':
			OPTIONS.daemonize = 0;
			break;

		case 'c':
			free(OPTIONS.commands);
			OPTIONS.commands = strdup(optarg);
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

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (OPTIONS.daemonize) {
		log_open("dbolo", "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			return 3;
		}
		umask(um);
	} else {
		log_open("dbolo", "console");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");

	logger(LOG_DEBUG, "allocating 0MQ context");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		return 3;
	}

	LIST(COMMANDS);
	if (s_parse(&COMMANDS, OPTIONS.commands) != 0)
		return 1;

	int epfd = epoll_create(42);
	if (epfd < 0) {
		logger(LOG_ERR, "failed to get an epoll fd");
		return 1;
	}

	struct epoll_event ev, events[BOLO_EPOLL_MAXFD];
	for (;;) {
		int i, n;
		n = epoll_wait(epfd, events, BOLO_EPOLL_MAXFD, AGENT_TICK_MS);

		uint64_t now = time_ms();
		now = now - now % 1000;

		command_t *cmd;
		pid_t pid;
		int status;

		for (i = 0; i < n; i++) {
			if (events[i].events & EPOLLIN) {
				for_each_object(cmd, &COMMANDS, l) {
					if (events[i].data.fd != cmd->fd)
						continue;

					logger(LOG_INFO, "checking child %u (fd=%x)", cmd->pid, cmd->fd);
					size_t nread;
					if ((nread = read(cmd->fd, cmd->buffer + cmd->nread, 8192 - cmd->nread)) < 0) {
						logger(LOG_WARNING, "read failed [child %i]: %s", cmd->pid, strerror(errno));
						continue;
					}
					logger(LOG_INFO, "read %i bytes", nread);
					cmd->nread += nread;

					char *nl = strchr(cmd->buffer, '\n');
					if (nl) {
						*nl++ = '\0';
						s_send(zmq, OPTIONS.endpoint, cmd->buffer);
						memmove(cmd->buffer, nl, 8192 - (nl - cmd->buffer));
						cmd->nread -= (nl - cmd->buffer);
					}
				}
			}
		}

		for_each_object(cmd, &COMMANDS, l) {
			if (cmd->pid > 0 && (pid = waitpid(cmd->pid, &status, WNOHANG)) == cmd->pid) {
				logger(LOG_INFO, "child %i exited %02x, next run at %lu", cmd->pid, status, cmd->next_run);

				close(cmd->fd);
				cmd->pid = -1;
				cmd->fd  = -1;

			} else if (cmd->next_run < now) {
				int pfd[2];
				if (pipe(pfd) != 0) {
					logger(LOG_ERR, "failed to run collector; pipe(): %s", strerror(errno));
					continue;
				}

				pid = fork();
				if (pid == 0) {
					dup2(pfd[1], 1);
					printf("CANARY\n");
					close(pfd[0]); /* stdin */
					execl("/bin/sh", "sh", "-c", cmd->exec, NULL);
					return 99;
				}

				close(pfd[1]); /* 'stdout' */
				cmd->fd = pfd[0];

				ev.data.fd = cmd->fd;
				ev.events = EPOLLIN | EPOLLET;
				if (epoll_ctl(epfd, EPOLL_CTL_ADD, cmd->fd, &ev) != 0) {
					logger(LOG_ERR, "epoll_ctl failed: %s", strerror(errno));
				}

				cmd->nread = 0;
				cmd->pid = pid;
				cmd->next_run = now + cmd->interval;
				logger(LOG_INFO, "child %i executing `%s`", cmd->pid, cmd->exec);
			}
		}
	}
	return 0;
}
