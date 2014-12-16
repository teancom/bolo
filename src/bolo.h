#ifndef BOLO_H
#define BOLO_H

#include <stdint.h>
#include <vigor.h>

#define OK       0
#define WARNING  1
#define CRITICAL 2
#define UNKNOWN  3
#define PENDING  4

#define MAX_NSCA_HOSTNAME 64
#define MAX_NSCA_SERVICE  128
#define MAX_NSCA_OUTPUT   4096

typedef struct {
	int16_t   version;
	uint32_t  crc32;
	uint32_t  timestamp;
	int16_t   return_code;
	char      host[MAX_NSCA_HOSTNAME];
	char      service[MAX_NSCA_SERVICE];
	char      output[MAX_NSCA_OUTPUT];
} nsca_t;

typedef struct {
	char     *name;
	type_t   *type;
	int64_t   last_seen;
	int64_t   expiry;
	uint8_t   status;
	char     *summary;
} state_t;

typedef struct {
	uint16_t  freshness;
	uint8_t   stale_status;
	char     *stale_summary;
} type_t;

typedef struct {
	hash_t  states;
	hash_t  problems;
	hash_t  types;
	char   *config;
	char   *file;
} db_t;

db_t db_open(const char *config);
int db_write(db_t *db);
int db_close(db_t *db);

#endif
