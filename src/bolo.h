#ifndef BOLO_H
#define BOLO_H

#include <stdint.h>
#include <vigor.h>

#define OK       0
#define WARNING  1
#define CRITICAL 2
#define UNKNOWN  3
#define PENDING  4

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
