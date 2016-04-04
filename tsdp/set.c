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

int tsdp_set_version(tsdp_t *pdu, int version)
{
	if (pdu == NULL || version != TSDP_VERSION) {
		errno = EINVAL;
		return -errno;
	}

	pdu->version = version;
	return 0;
}

int tsdp_set_opcode(tsdp_t *pdu, int opcode)
{
	if (pdu == NULL || !__tsdp_valid_opcode(opcode)) {
		errno = EINVAL;
		return -errno;
	}

	pdu->opcode = opcode;
	return 0;
}

int tsdp_set_flags(tsdp_t *pdu, int flags)
{
	if (pdu == NULL || flags != (flags & 0xff)) {
		errno = EINVAL;
		return -errno;
	}

	pdu->flags = flags & 0xff;
	return 0;
}

int tsdp_set_payloads(tsdp_t *pdu, int payloads)
{
	if (pdu == NULL || payloads != (payloads & 0xffff)) {
		errno = EINVAL;
		return -errno;
	}

	pdu->payloads = payloads & 0xffff;
	return 0;
}
