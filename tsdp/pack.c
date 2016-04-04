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

int tsdp_pack(uint8_t **buf, size_t *len, const tsdp_t *pdu)
{
	if (buf == NULL || len == NULL || pdu == NULL) {
		errno = EINVAL;
		return -errno;
	}

	if (pdu->octets < 4) {
		errno = TSDP_E2SMALL;
		return -errno;
	}

	*len = (size_t)pdu->octets;
	*buf = calloc(*len, sizeof(uint8_t));

	size_t i = 0;
	*buf[i++] = ((pdu->version  & 0xf) << 4)
	          | ((pdu->opcode   & 0xf));
	*buf[i++] = ((pdu->flags    & 0xff));
	*buf[i++] = ((pdu->payloads & 0xff00) >> 8);
	*buf[i++] = ((pdu->payloads & 0x00ff));

	struct __tsdp_frame *frame = pdu->frames;
	while (frame != NULL) {
		if (pdu->octets < i + 2 + frame->size) {
			free(*buf);
			*buf = NULL;
			*len = 0;
			errno = TSDP_E2SMALL;
			return -errno;
		}
		*buf[i++] = ((frame->type & 0x7) << 4)
		          | ((frame->next == NULL ? 0 : 0x8))
		          | ((frame->size & 0xf00) >> 8);
		*buf[i++] = ((frame->size & 0x0ff));

		size_t j;
		for (j = 0; j < frame->size; j++, i++) {
			*buf[i] = frame->data[j];
		}
	}

	return 0;
}
