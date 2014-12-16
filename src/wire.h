#ifndef WIRE_H
#define WIRE_H

#include "bolo.h"

#define MAX_NSCA_HOSTNAME 64
#define MAX_NSCA_SERVICE  128
#define MAX_NSCA_OUTPUT   4096

typedef struct {
	int16_t   version;
	uint32_t  crc32;
	uint32_t  timestamp;
	int16_t   status;
	char      host[MAX_NSCA_HOSTNAME];
	char      service[MAX_NSCA_SERVICE];
	char      output[MAX_NSCA_OUTPUT];
} nsca_t;

typedef struct {
	uint8_t   version;     /* version of the BOLO packet format in use       */
	uint32_t  length;      /* length of entire packet, including 'version'   */
	uint32_t  timestamp;   /* time of original submission                    */
	uint8_t   status;      /* numeric status (OK|WARNING|CRITICAL|UNKNOWN)   */
	char      payload[];   /* host + output, as null-terminated strings      */
} bolo_t;

#endif
