#ifndef BOLO_H
#define BOLO_H

#include <stdint.h>
#include <vigor.h>

#define PACKED __attribute__((packed))

#define OK       0
#define WARNING  1
#define CRITICAL 2
#define UNKNOWN  3
#define PENDING  4

#define DEFAULT_NSCA_PORT 5667
#define DEFAULT_BOLO_ENDPOINT "tcp://*:2998"
#define DEFAULT_STAT_ENDPOINT "tcp://*:2999"

#define DB_MANAGER_ENDPOINT "inproc://db"

#define MIN_BOLO_LEN 12+2

typedef struct {
	uint8_t   version;     /* version of the BOLO packet format in use       */
	uint8_t   status;      /* numeric status (OK|WARNING|CRITICAL|UNKNOWN)   */
	uint16_t  _RESERVED_1;
	uint32_t  length;      /* length of entire packet, including 'version'   */
	uint32_t  timestamp;   /* time of original submission                    */
	char      payload[];   /* host + output, as null-terminated strings      */
} bolo_t;

typedef struct {
	uint16_t  freshness;
	uint8_t   stale_status;
	char     *stale_summary;
} type_t;

typedef struct {
	char     *name;
	type_t   *type;
	int64_t   last_seen;
	int64_t   expiry;
	uint8_t   status;
	char     *summary;
	uint8_t   stale;
} state_t;

typedef struct {
	hash_t  states;
	hash_t  problems;
	hash_t  types;
	char   *config;
	char   *file;
} db_t;

typedef struct {
	void   *zmq;
	int     nsca_socket;
	void   *bolo_zocket;
	void   *stat_zocket;

	char   *dumpfile_fmt;

	struct {
		uint16_t freshness;
		uint16_t savestate;
	} interval;
} server_t;

typedef struct PACKED {
	uint32_t  magic;
	uint16_t  version;
	uint16_t  flags;
	 int64_t  timestamp;
	uint32_t  count;
} binf_header_t;

typedef struct PACKED {
	uint16_t  len;
	 int64_t  last_seen;
	 uint8_t  status;
	 uint8_t  stale;
	    char  payload[];
} binf_record_t;

/* threads */
void* nsca_listener(void *u);
void* bolo_listener(void *u);
void* stat_listener(void *u);
void* db_manager(void *u);
void* scheduler(void *u);

db_t db_open(const char *config);
int db_write(db_t *db);
int db_close(db_t *db);

#endif
