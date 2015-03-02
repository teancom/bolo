/*
  Copyright 2015 James Hunt <james@jameshunt.us>

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

#include "bolo.h"

pdu_t *vx_pdu_dup(pdu_t *from, const char *type)
{
	pdu_t *to = pdu_make(type ? type : pdu_type(from), 0);
	if (vx_pdu_copy(to, from, 1, 0) != 0) {
		pdu_free(to);
		return NULL;
	}
	return to;
}

int vx_pdu_copy(pdu_t *to, pdu_t *from, int start, int n)
{
	int i, end = n ? start + n : pdu_size(from);
	for (i = start; i < pdu_size(from) && i < end; i++) {
		size_t len;
		uint8_t *buf = pdu_segment(from, i, &len);
		pdu_extend(to, buf, len);
		free(buf);
	}
	return 0;
}

int vx_vzmq_connect(void *z, const char *endpoint)
{
	strings_t *names = vzmq_resolve(endpoint, AF_UNSPEC);
	if (names->num == 0) {
		errno = ENOENT;
		return 1;
	}

	int rc;
	unsigned int i;
	for (i = 0; i < names->num; i++) {
		logger(LOG_DEBUG, "trying endpoint %s (from %s)", names->strings[i], endpoint);
		rc = zmq_connect(z, names->strings[i]);
		if (rc == 0)
			break;
	}

	strings_free(names);
	return rc;
}
