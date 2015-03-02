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

typedef struct {
	server_t *server;
	void     *client;
} scheduler_t;

static void* cleanup_scheduler(void *_)
{
	scheduler_t *s = (scheduler_t*)_;

	if (s->client) {
		logger(LOG_INFO, "scheduler cleaning up; closing kernel client socket");
		vzmq_shutdown(s->client, 0);
	}

	free(s);
	return NULL;
}

void* scheduler(void *u)
{
	scheduler_t *s = calloc(1, sizeof(scheduler_t));
	pthread_cleanup_push(cleanup_scheduler, s);

	s->server = (server_t*)u;
	if (!s->server) {
		logger(LOG_CRIT, "scheduler failed: server context was NULL");
		return NULL;
	}

	if (!s->server->interval.tick)
		s->server->interval.tick = 1000;

	s->client = zmq_socket(s->server->zmq, ZMQ_DEALER);
	if (!s->client) {
		logger(LOG_CRIT, "scheduler failed to get a DEALER socket");
		return NULL;
	}

	if (vx_vzmq_connect(s->client, KERNEL_ENDPOINT) != 0) {
		logger(LOG_CRIT, "scheduler failed to connect to kernel at " KERNEL_ENDPOINT);
		return NULL;
	}

	uint16_t freshness = s->server->interval.freshness;
	uint16_t savestate = s->server->interval.savestate;
	for (;;) {
		if (--freshness == 0) {
			pdu_send_and_free(pdu_make("CHECKFRESH", 0), s->client);
			pdu_free(pdu_recv(s->client));
			freshness = s->server->interval.freshness;
		}

		if (--savestate == 0) {
			pdu_send_and_free(pdu_make("SAVESTATE", 0), s->client);
			pdu_free(pdu_recv(s->client));
			savestate = s->server->interval.savestate;
		}

		pdu_send_and_free(pdu_make("TICK", 0), s->client);
		pdu_free(pdu_recv(s->client));

		sleep_ms(s->server->interval.tick);
	}

	pthread_cleanup_pop(1);
	return NULL;
}
