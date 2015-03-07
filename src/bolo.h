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

#ifndef BOLO_H
#define BOLO_H

#include <stdint.h>
#include <vigor.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pcre.h>

#define PACKED __attribute__((packed))

#include "bits.h"

#define OK       0
#define WARNING  1
#define CRITICAL 2
#define UNKNOWN  3
#define PENDING  4

#define DEFAULT_CONFIG_FILE "/etc/bolo.conf"

#define DEFAULT_LISTENER     "tcp://*:2999"
#define DEFAULT_CONTROLLER   "tcp://127.0.0.1:2998"
#define DEFAULT_BROADCAST    "tcp://*:2997"
#define DEFAULT_LOG_LEVEL    "error"
#define DEFAULT_LOG_FACILITY "daemon"
#define DEFAULT_RUNAS_USER   "bolo"
#define DEFAULT_RUNAS_GROUP  "bolo"
#define DEFAULT_PIDFILE      "/var/run/bolo.pid"
#define DEFAULT_SAVEFILE     "/var/lib/bolo/save.db"
#define DEFAULT_DUMPFILES    "/var/tmp/bolo.%s"

#define KERNEL_ENDPOINT "inproc://kernel"

typedef struct {
	uint16_t  freshness;
	uint8_t   status;
	char     *summary;
} type_t;

typedef struct {
	int32_t  time;
} window_t;

typedef struct {
	type_t   *type;
	char     *name;
	int32_t   last_seen;
	int32_t   expiry;
	uint8_t   status;
	char     *summary;
	uint8_t   stale;
} state_t;

typedef struct {
	list_t      l;
	type_t     *type;

	pcre       *re;
	pcre_extra *re_extra;
} re_state_t;

typedef struct {
	window_t *window;
	char     *name;
	int32_t   last_seen;
	uint64_t  value;
} counter_t;

typedef struct {
	list_t      l;
	window_t   *window;

	pcre       *re;
	pcre_extra *re_extra;
} re_counter_t;

typedef struct {
	window_t *window;
	char     *name;
	int32_t   last_seen;

	uint64_t  n;
	double    min;
	double    max;
	double    sum;
	double    mean, mean_;
	double    var,  var_;
} sample_t;

typedef struct {
	list_t      l;
	window_t   *window;

	pcre       *re;
	pcre_extra *re_extra;
} re_sample_t;

typedef struct {
	window_t   *window;
	char       *name;
	int32_t     first_seen;
	int32_t     last_seen;

	uint8_t     started;

	uint64_t    first;
	uint64_t    last;
} rate_t;

typedef struct {
	list_t      l;
	window_t   *window;

	pcre       *re;
	pcre_extra *re_extra;
} re_rate_t;

typedef struct {
	list_t     l;
	int32_t    timestamp;
	char      *name;
	char      *extra;
} event_t;

typedef struct {
	hash_t  states;
	hash_t  counters;
	hash_t  samples;
	hash_t  rates;

	list_t  events;
	int     events_count;

	list_t  state_matches;
	list_t  counter_matches;
	list_t  sample_matches;
	list_t  rate_matches;

	hash_t  types;
	hash_t  windows;
} db_t;

#define EVENTS_KEEP_NUMBER 0
#define EVENTS_KEEP_TIME   1

typedef struct {
	void   *zmq;
	db_t    db;
	hash_t  keys;

	struct {
		char     *listener;
		char     *controller;
		char     *broadcast;
		uint16_t  nsca_port;

		char     *log_level;
		char     *log_facility;

		char     *pidfile;
		char     *runas_user;
		char     *runas_group;
		char     *savefile;
		char     *keysfile;
		char     *eventsfile;
		char     *dumpfiles;

		int       events_max;
		int       events_keep;
	} config;

	struct {
		uint16_t tick; /* ms */
		uint16_t freshness;
		uint16_t savestate;
	} interval;
} server_t;

int configure(const char *path, server_t *s);
int deconfigure(server_t *s);

void sample_reset(sample_t *sample);
int sample_data(sample_t *s, double v);

void counter_reset(counter_t *counter);

void rate_reset(rate_t *r);
int rate_data(rate_t *r, uint64_t v);
double rate_calc(rate_t *r, int32_t span);

/* threads */
void* listener(void *u);
void* nsca_gateway(void *u);
void* controller(void *u);
void* kernel(void *u);
void* scheduler(void *u);

/* utilities - candidates for libvigor */
pdu_t *vx_pdu_dup(pdu_t *orig, const char *type);
int vx_pdu_copy(pdu_t *to, pdu_t *from, int start, int n);
int vx_timespec_parse(char *s, int modulo);
int vx_vzmq_connect(void *z, const char *endpoint);

#endif
