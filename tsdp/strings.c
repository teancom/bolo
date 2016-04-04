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

static const char *__tsdp_errors[] = {
	"Success!",
	"Buffer too size for requested operation", /* TSDP_E2SMALL   */
	"Early final frame found",                 /* TSDP_EEARLYFF  */
	"No final frame found",                    /* TSDP_ENOFF     */
	"Frame type mismatch",                     /* TSDP_EBADTYPE  */
};

const char* tsdp_error_str(int error)
{
	if (error < 0 || error > TSDP_BIGGEST_ERROR) {
		return "(uh-oh, that error number is out-of-bounds...)";
	}
	return __tsdp_errors[error];
}

static const char *__tsdp_opcodes[] = {
	"HEARTBEAT",
	"SUBMIT",
	"BROADCAST",
	"FORGET",
	"REPLAY",
	"SUBSCRIBER",
};

const char* tsdp_opcode_str(int opcode)
{
	if (opcode < 0 || opcode > TSDP_BIGGEST_OPCODE) {
		return "<unknown>";
	}
	return __tsdp_opcodes[opcode];
}
