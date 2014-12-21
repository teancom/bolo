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

#define PACKED __attribute__((packed))

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

#define DB_MANAGER_ENDPOINT "inproc://db"

typedef struct {
	uint16_t  freshness;
	uint8_t   status;
	char     *summary;
} type_t;

typedef struct {
	type_t   *type;
	int32_t   last_seen;
	int32_t   expiry;
	uint8_t   status;
	char     *summary;
	uint8_t   stale;
} state_t;

typedef struct {
	hash_t  states;
	hash_t  types;
} db_t;

typedef struct {
	void   *zmq;
	db_t    db;

	struct {
		char     *listener;
		char     *controller;
		char     *broadcast;

		char     *log_level;
		char     *log_facility;

		char     *pidfile;
		char     *runas_user;
		char     *runas_group;
		char     *savefile;
		char     *dumpfiles;
	} config;

	struct {
		uint16_t tick; /* ms */
		uint16_t freshness;
		uint16_t savestate;
	} interval;
} server_t;

int configure(const char *path, server_t *s);
int deconfigure(server_t *s);

/* threads */
void* listener(void *u);
void* controller(void *u);
void* db_manager(void *u);
void* scheduler(void *u);

#endif
