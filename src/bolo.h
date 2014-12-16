#ifndef BOLO_H
#define BOLO_H

#include <stdint.h>
#include <vigor.h>

#define OK       0
#define WARNING  1
#define CRITICAL 2
#define UNKNOWN  3
#define PENDING  4

#define NSCA_PACKET_LEN 12 + 64 + 128 + 4096

typedef struct {
	uint32_t timestamp;   /* time of original submission                  */
	char    *name;        /* name of the state, heap-allocated            */
	char    *summary;     /* summary of the state, heap-allocated         */
	uint8_t  status;      /* numeric status (OK|WARNING|CRITICAL|UNKNOWN) */

	void    *_packet;     /* (internal use)                               */
} result_t;

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
} state_t;

typedef struct {
	hash_t  states;
	hash_t  problems;
	hash_t  types;
	char   *config;
	char   *file;
} db_t;

/* threads */
void* nsca_listener(void *u);
void* bolo_listener(void *u);
void* stat_listener(void *u);
void* db_manager(void *u);
void* scheduler(void *u);

/* packet/result handling */
result_t* nsca_result(const char *buf, size_t len);
result_t* bolo_result(const char *buf, size_t len);
void result_free(result_t *r);

db_t db_open(const char *config);
int db_write(db_t *db);
int db_close(db_t *db);

#endif
