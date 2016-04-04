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

#ifndef __TSDP_INTERNAL_H
#define __TSDP_INTERNAL_H

struct __tsdp_frame {
	/* type of data encoded in this frame.
	   one of the TSDP_TYPE_* constants */
	int type;

	/* how many octets of data are in this frame. */
	int size;

	/* the actual data, in raw network form. */
	uint8_t *data;

	/* pointer to the next __tsdp_frame structure
	   for the current pdu, or NULL if this is the
	   final frame */
	struct __tsdp_frame *next;
};

struct __tsdp_pdu {
	/* version of the tsdp protocol in force. */
	int version;

	/* numeric opcode for this pdu.
	 * one of the TSDP_OPCODE_* constants. */
	int opcode;

	/* flags for this pdu, for altering behavior
	   of opcode + payloads combination.  one or
	   more TSDP_F_* constants OR'd together. */
	int flags;

	/* payload types that this pdu applies to.
	   one or more TSDP_TYPE_* constants OR'd together
	   (multiple values may not be permitted, based
	    on the opcode in use). */
	int payloads;

	/* a pointer to the first frame of this pdu,
	   or NULL if there are no frames. */
	struct __tsdp_frame *frames;

	/* a cache counter that tracks how many frames
	   exist in the frames list, for basic sanity
	   checking of frame accessor functions (i.e.
	   for out-of-bounds errors). */
	int nframes;

	/* a cache variable that keeps track of how many
	   octets it would take to binary-pack this pdu. */
	int octets;
};

#define __tsdp_valid_opcode(x) (((x) & TSDP_RESERVED_OP)   == 0)
#define __tsdp_valid_type(x)   (((x) & TSDP_RESERVED_TYPE) == 0)

#endif
