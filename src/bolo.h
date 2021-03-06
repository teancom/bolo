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

#ifndef BOLO_INTERNALS_H
#define BOLO_INTERNALS_H
#include "config.h"
#define BOLO_VERSION PACKAGE_VERSION

#include <stdint.h>
#include <vigor.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pcre.h>

#if !HAVE_PCRE_FREE_STUDY
#define pcre_free_study pcre_free
#endif

#define PACKED __attribute__((packed))

#define OK       0
#define WARNING  1
#define CRITICAL 2
#define UNKNOWN  3
#define PENDING  4

#define PAYLOAD_STATE     0x0001
#define PAYLOAD_COUNTER   0x0002
#define PAYLOAD_RATE      0x0004
#define PAYLOAD_SAMPLE    0x0008
#define PAYLOAD_EVENT     0x0010
#define PAYLOAD_FACT      0x0020
#define PAYLOAD_RESERVED  0xFF70
#define PAYLOAD_ALL       0xFFFF

#define DEFAULT_CONFIG_FILE "/etc/bolo.conf"
#define DEFAULT_AGENT_FILE  "/etc/dbolo.conf"

#define DEFAULT_LISTENER     "tcp://*:2999"
#define DEFAULT_CONTROLLER   "tcp://127.0.0.1:2998"
#define DEFAULT_BROADCAST    "tcp://*:2997"
#define DEFAULT_LOG_LEVEL    "error"
#define DEFAULT_LOG_FACILITY "daemon"
#define DEFAULT_RUNAS_USER   "bolo"
#define DEFAULT_RUNAS_GROUP  "bolo"
#define DEFAULT_PIDFILE      "/var/run/bolo.pid"
#define DEFAULT_SAVEFILE     "/var/lib/bolo/save.db"
#define DEFAULT_KEYSFILE     "/var/lib/bolo/keys.db"
#define DEFAULT_GRACE_PERIOD 15
#define DEFAULT_SWEEP        60
#define DEFAULT_SAVE_SIZE     4
#define DEFAULT_SAVE_INTERVAL 15

#define KERNEL_ENDPOINT "inproc://kernel"

typedef struct {
	char     *name;
	uint16_t  freshness;
	uint8_t   status;
	char     *summary;
} type_t;

typedef struct {
	char     *name;
	int32_t  time;

	list_t   anon;
} window_t;

typedef struct {
	type_t   *type;
	char     *name;
	int32_t   last_seen;
	int32_t   expiry;
	uint8_t   status;
	char     *summary;
	uint8_t   stale;
	uint8_t   ignore;
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
	uint8_t   ignore;
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
	uint8_t   ignore;
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

	uint64_t    first;
	uint64_t    last;
	uint8_t     ignore;
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
	list_t  anon_windows;
} db_t;

#define EVENTS_KEEP_NUMBER 0
#define EVENTS_KEEP_TIME   1

typedef struct {
	db_t    db;
	hash_t  keys;

	struct {
		char     *listener;
		char     *controller;
		char     *broadcast;
		char     *beacon;
		uint16_t  nsca_port;

		char     *log_level;
		char     *log_facility;

		char     *pidfile;
		char     *runas_user;
		char     *runas_group;
		char     *savefile;
		char     *keysfile;

		int       save_size;

		int       interval;
		int       events_max;
		int       events_keep;

		int       grace_period;
	} config;

	struct {
		uint16_t tick; /* ms */
		uint16_t freshness;
		uint16_t sweep;
		uint16_t savestate;
	} interval;
} server_t;

#define probable(f) (rand() * 1.0 / RAND_MAX <= (f))

/* write to the mmap memory space */
int binf_write(db_t *db, const char *file, int db_size);
int binf_read(db_t *db, const char *file, int db_size);
/* force a flush to disk of the mmap save file */
int binf_sync(const char *file, int db_size);

int configure(const char *path, server_t *s);
int deconfigure(server_t *s);

void sample_reset(sample_t *sample);
int sample_data(sample_t *s, double v);

void counter_reset(counter_t *counter);

void rate_reset(rate_t *r);
int rate_data(rate_t *r, uint64_t v);
double rate_calc(rate_t *r, int32_t span);

state_t*   find_state(  db_t*, const char *name);
counter_t* find_counter(db_t*, const char *name);
sample_t*  find_sample( db_t*, const char *name);
rate_t*    find_rate(   db_t*, const char *name);

pdu_t *parse_state_pdu  (int argc, char **argv, const char *ts);
pdu_t *parse_counter_pdu(int argc, char **argv, const char *ts);
pdu_t *parse_sample_pdu (int argc, char **argv, const char *ts);
pdu_t *parse_rate_pdu   (int argc, char **argv, const char *ts);
pdu_t *parse_setkeys_pdu(int argc, char **argv);
pdu_t *parse_event_pdu  (int argc, char **argv, const char *ts);
pdu_t *stream_pdu(const char *line);

pdu_t *forget_pdu  (uint16_t payload, const char *regex, uint8_t ignore);
pdu_t *state_pdu   (const char *name, int status, const char *msg);
pdu_t *counter_pdu (const char *name, unsigned int value);
pdu_t *sample_pdu  (const char *name, int n, ...);
pdu_t *rate_pdu    (const char *name, unsigned long value);
pdu_t *setkeys_pdu (int n, ...);
pdu_t *event_pdu   (const char *name, const char *extra);

/* threads */
int core_scheduler_thread(void *zmq, int interval);
int core_kernel_thread(void *zmq, server_t *server);
int core_supervisor(void *zmq);

/* utilities - candidates for libvigor */
int vx_vzmq_connect(void *z, const char *endpoint);

#endif
