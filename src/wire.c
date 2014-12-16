#include "bolo.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#define MAX_NSCA_HOSTNAME 64
#define MAX_NSCA_SERVICE  128
#define MAX_NSCA_OUTPUT   4096

typedef struct {
	uint32_t  crc32;
	uint32_t  timestamp;
	int16_t   version;
	int16_t   status;
	char      host[MAX_NSCA_HOSTNAME];
	char      service[MAX_NSCA_SERVICE];
	char      output[MAX_NSCA_OUTPUT];
} nsca_t;

#define MIN_BOLO_LEN 12+2

typedef struct {
	uint8_t   version;     /* version of the BOLO packet format in use       */
	uint8_t   status;      /* numeric status (OK|WARNING|CRITICAL|UNKNOWN)   */
	uint16_t  _RESERVED_1;
	uint32_t  length;      /* length of entire packet, including 'version'   */
	uint32_t  timestamp;   /* time of original submission                    */
	char      payload[];   /* host + output, as null-terminated strings      */
} bolo_t;

result_t* nsca_result(const char *buf, size_t len)
{
	return NULL;
}

result_t* bolo_result(const char *buf, size_t len)
{
	if (len < MIN_BOLO_LEN) return NULL;

	bolo_t *pkt = malloc(len);
	memcpy(pkt, buf, len);

	result_t *R = malloc(sizeof(result_t));
	memset(R, 0, sizeof(result_t));
	R->timestamp = ntohl(pkt->timestamp);
	R->status    = pkt->status;
	R->name      = pkt->payload;
	R->summary   = pkt->payload + strlen(R->name) + 1;
	R->_packet   = pkt;

	return R;
}

void result_free(result_t *r)
{
	if (r) {
		free(r->_packet);
	}
	free(r);
}
