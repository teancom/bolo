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
#include <string.h>
#include <errno.h>
#include "internal.h"

static struct __tsdp_frame* _nthframe(const tsdp_t *pdu, int n)
{
	struct __tsdp_frame *frame = pdu->frames;
	while (frame != NULL && n > 0) {
		frame = frame->next;
		n--;
	}
	return frame;
}

int tsdp_get_version(const tsdp_t *pdu)
{
	if (pdu == NULL) {
		errno = EINVAL;
		return -errno;
	}

	return pdu->version;
}

int tsdp_get_opcode(const tsdp_t *pdu)
{
	if (pdu == NULL) {
		errno = EINVAL;
		return -errno;
	}

	return pdu->opcode;
}

int tsdp_get_flags(const tsdp_t *pdu)
{
	if (pdu == NULL) {
		errno = EINVAL;
		return -errno;
	}

	return pdu->flags;
}

int tsdp_get_payloads(const tsdp_t *pdu)
{
	if (pdu == NULL) {
		errno = EINVAL;
		return -errno;
	}

	return pdu->payloads;
}

int tsdp_get_size(const tsdp_t *pdu)
{
	if (pdu == NULL) {
		errno = EINVAL;
		return -errno;
	}

	return pdu->nframes;
}

int tsdp_get_frame_type(const tsdp_t *pdu, int n)
{
	if (pdu == NULL || n > pdu->nframes) {
		errno = EINVAL;
		return -errno;
	}

	struct __tsdp_frame *frame = _nthframe(pdu, n);
	if (!frame) {
		errno = EINVAL;
		return -errno;
	}

	return frame->type;
}

int tsdp_get_frame_size(const tsdp_t *pdu, int n)
{
	if (pdu == NULL || n > pdu->nframes) {
		errno = EINVAL;
		return -errno;
	}

	struct __tsdp_frame *frame = _nthframe(pdu, n);
	if (!frame) {
		errno = EINVAL;
		return -errno;
	}

	return frame->size;
}

int tsdp_get_frame_value(const tsdp_t *pdu, int n, int type, void **data, int *len)
{
	if (pdu == NULL || data == NULL || len == NULL
	 || n > pdu->nframes || !__tsdp_valid_type(type)) {
		errno = EINVAL;
		return -errno;
	}

	struct __tsdp_frame *frame = _nthframe(pdu, n);
	if (!frame) {
		errno = EINVAL;
		return -errno;
	}

	if (frame->type != type) {
		errno = TSDP_EBADTYPE;
		return -errno;
	}

	*len  = frame->size;
	*data = malloc(*len);
	memcpy(*data, frame->data, *len);
	return 0;
}
