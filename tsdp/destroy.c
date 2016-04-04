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

int tsdp_destroy(tsdp_t *pdu)
{
	if (pdu == NULL) {
		return 0;
	}

	struct __tsdp_frame *next, *frame = pdu->frames;
	while (frame != NULL) {
		next = frame->next;

		free(frame->data);
		free(frame);

		frame = next;
	}

	free(pdu);
	return 0;
}
