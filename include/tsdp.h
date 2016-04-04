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

#ifndef TSDP_H
#define TSDP_H

#include <sys/types.h>
#include <stdint.h>

typedef struct __tsdp_pdu tsdp_t;
typedef long long         tsdp_tstamp_t;

#define  TSDP_VERSION  1

#define  TSDP_OP_HEARTBEAT   0
#define  TSDP_OP_SUBMIT      0x01
#define  TSDP_OP_BROADCAST   0x02
#define  TSDP_OP_FORGET      0x03
#define  TSDP_OP_REPLAY      0x04
#define  TSDP_OP_SUBSCRIBER  0x05
#define  TSDP_BIGGEST_OPCODE TSDP_OP_SUBSCRIBER
#define  TSDP_RESERVED_OP    0xfa

#define  TSDP_PAYLOAD_NONE      0
#define  TSDP_PAYLOAD_SAMPLE    0x0001
#define  TSDP_PAYLOAD_TALLY     0x0002
#define  TSDP_PAYLOAD_DELTA     0x0004
#define  TSDP_PAYLOAD_STATE     0x0008
#define  TSDP_PAYLOAD_EVENT     0x0010
#define  TSDP_PAYLOAD_FACT      0x0020
#define  TSDP_RESERVED_PAYLOAD  0xff70

#define  TSDP_TYPE_NIL       0
#define  TSDP_TYPE_UINT      0x01
#define  TSDP_TYPE_FLOAT     0x02
#define  TSDP_TYPE_STRING    0x03
#define  TSDP_TYPE_TSTAMP    0x04
#define  TSDP_RESERVED_TYPE  0xfb

#define  TSDP_F_ROLLOVER            0x80
#define  TSDP_F_UNSUBSCRIBE         0x80
#define  TSDP_F_STATE_OK            0
#define  TSDP_F_STATE_WARNING       0x01
#define  TSDP_F_STATE_CRITICAL      0x02
#define  TSDP_F_STATE_ERROR         0x03
#define  TSDP_F_STATE_WAS_OK        (TSDP_F_STATE_OK        <<  2)
#define  TSDP_F_STATE_WAS_WARNING   (TSDP_F_STATE_WARNING   <<  2)
#define  TSDP_F_STATE_WAS_CRITICAL  (TSDP_F_STATE_CRITICAL  <<  2)
#define  TSDP_F_STATE_WAS_ERROR     (TSDP_F_STATE_ERROR     <<  2)
#define  TSDP_F_STATE_FRESH         0x80
#define  TSDP_F_STATE_STALE         0
#define  TSDP_F_STATE_STEADY        0
#define  TSDP_F_STATE_TRANSITION    0x40
#define  TSDP_F_DELTA_UNIT_CENTMS   0
#define  TSDP_F_DELTA_UNIT_SECOND   0x01
#define  TSDP_F_DELTA_UNIT_MINUTE   0x02
#define  TSDP_F_DELTA_UNIT_HOUR     0x03
#define  TSDP_F_DELTA_UNIT_DAY      0x04

#define  TSDP_E2SMALL  1
#define  TSDP_EEARLYFF 2
#define  TSDP_ENOFF    3
#define  TSDP_EBADTYPE 4
#define  TSDP_BIGGEST_ERROR  TSDP_EBADTYPE

const char* tsdp_error_str(int error);
const char* tsdp__opcode_str(int opcode);

int tsdp_pack(uint8_t **buf, size_t *len, const tsdp_t *pdu);
int tsdp_unpack(tsdp_t **pdu, const uint8_t *buf, size_t len);

int tsdp_create(tsdp_t **pdu, int version, int opcode);
int tsdp_destroy(tsdp_t *pdu);

int tsdp_get_version  (const tsdp_t *pdu);
int tsdp_get_opcode   (const tsdp_t *pdu);
int tsdp_get_flags    (const tsdp_t *pdu);
int tsdp_get_payloads (const tsdp_t *pdu);
int tsdp_get_size     (const tsdp_t *pdu);

int tsdp_get_frame_type  (const tsdp_t *pdu, int frame);
int tsdp_get_frame_size  (const tsdp_t *pdu, int frame);
int tsdp_get_frame_value (const tsdp_t *pdu, int frame, int type, void **data, int *len);

int tsdp_set_version  (tsdp_t *pdu, int version);
int tsdp_set_opcode   (tsdp_t *pdu, int opcode);
int tsdp_set_flags    (tsdp_t *pdu, int flags);
int tsdp_set_payloads (tsdp_t *pdu, int payloads);

int tsdp_extend(tsdp_t *pdu, int type, void *data, int len);

#endif
