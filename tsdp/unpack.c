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

#include <tsdp.h>
#include <stdlib.h>
#include <errno.h>
#include "internal.h"

int tsdp_unpack(tsdp_t **pdu, const uint8_t *buf, size_t len)
{
	int rc;
	if (pdu == NULL || buf == NULL || len == 0) {
		errno = EINVAL;
		return -errno;
	}

	if (len < 4) {
		errno = TSDP_E2SMALL;
		return -errno;
	}

	*pdu = malloc(sizeof(struct __tsdp_frame));
	if (!pdu) {
		errno = ENOMEM;
		return -errno;
	}
	(*pdu)->octets = len; /* speculative for now, but will be born out by
	                         the remainder of this function (if false, we'll
	                         bail out and return an error to the caller...) */

	size_t i = 0;
	rc = tsdp_set_version((*pdu), (buf[i] >> 4) & 0xf);
	if (rc != 0) {
		tsdp_destroy(*pdu);
		*pdu = NULL;
		return rc;
	}

	rc = tsdp_set_opcode((*pdu), (buf[i] & 0xf));
	if (rc != 0) {
		tsdp_destroy(*pdu);
		*pdu = NULL;
		return rc;
	}

	i++;
	rc = tsdp_set_flags((*pdu), buf[i]);
	if (rc != 0) {
		tsdp_destroy(*pdu);
		*pdu = NULL;
		return rc;
	}

	i++;
	rc = tsdp_set_payloads((*pdu), (buf[i] << 8) | (buf[i+1]));
	if (rc != 0) {
		tsdp_destroy(*pdu);
		*pdu = NULL;
		return rc;
	}

	i += 2;
	struct __tsdp_frame **next = &((*pdu)->frames),
	                     *frame;
	while (i < len) {
		frame = malloc(sizeof(struct __tsdp_frame));
		if (!frame) {
			tsdp_destroy(*pdu);
			*pdu = NULL;
			errno = ENOMEM;
			return -errno;
		}

		int final = (buf[i] & 0x80);
		frame->type = ((buf[i] >> 4) & 0x3);
		frame->size = ((buf[i] & 0x0f) << 8); /* higher nibble of size ... */

		i++;
		if (i >= len) {
			tsdp_destroy(*pdu);
			*pdu = NULL;
			free(frame);
			errno = TSDP_E2SMALL;
			return -errno;
		}

		frame->size += buf[i];
		i++;
		if (i + frame->size >= len) {
			tsdp_destroy(*pdu);
			*pdu = NULL;
			free(frame);
			errno = TSDP_E2SMALL;
			return -errno;
		}

		size_t j;
		for (j = 0; j < frame->size; j++, i++) {
			frame->data[j] = buf[i];
		}

		*next = frame;
		next = &frame->next;
		(*pdu)->nframes++;

		if (final) {
			if (i < len) {
				tsdp_destroy(*pdu);
				*pdu = NULL;
				errno = TSDP_EEARLYFF;
				return -errno;
			}

		} else if (i == len) {
			tsdp_destroy(*pdu);
			*pdu = NULL;
			errno = TSDP_ENOFF;
			return -errno;
		}
	}

	return 0;
}
