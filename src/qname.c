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

#include "bolo.h"

static int s_qname_part_sort(const void *a_, const void *b_)
{
	qname_part_t *a = *(qname_part_t**)a_;
	qname_part_t *b = *(qname_part_t**)b_;
	return a->name && b->name ? strcmp(a->name, b->name) : 0;
}

qname_t* qname_parse(const char *name)
{
	qname_t *qn;
	qname_part_t *parts[1024] = {0};
	const char *a, *b;
	int i;

	qn = vmalloc(sizeof(qname_t));

	/* degenerate case */
	if (!*name)
		return qn;

	for (i = 0, a = name; i < 1024;) {
		for (b = a; *b && *b != '=' && *b != ','; b++)
			;
		if (!*b || *b == ',') {
			if (b - a == 1 && strncmp(a, "*", 1) == 0) {
				qn->wildcard = 1;

				if (!*b)
					break;

				a = b + 1;
				continue;
			}
			return NULL;
		}

		parts[i] = vmalloc(sizeof(qname_part_t));
		parts[i]->name = strndup(a, b - a);
		for (a = ++b; *b && *b != ','; b++)
			;
		parts[i]->wildcard = strncmp(a, "*", b - a) == 0 ? 1 : 0;
		parts[i]->value = strndup(a, b - a);
		i++;

		if (!*b)
			break;
		a = b + 1;
	}

	qsort(parts, i, sizeof(qname_part_t*), s_qname_part_sort);
	qn->size = i;
	qn->parts = vcalloc(qn->size, sizeof(qname_part_t));
	for (i = 0; i < qn->size; i++) {
		qn->parts[i].name     = parts[i]->name;
		qn->parts[i].value    = parts[i]->value;
		qn->parts[i].wildcard = parts[i]->wildcard;
		free(parts[i]); /* but not name / value */
	}
	for (; i < 1024; i++)
		free(parts[i]); /* name / value are null */

	return qn;
}

char* qname_string(qname_t *qn)
{
	size_t len = 0;
	int i;

	for (i = 0; i < qn->size; i++) {
		len += strlen(qn->parts[i].name)  + 1 /* = */
		     + strlen(qn->parts[i].value) + 1 /* , */;
	}
	if (qn->wildcard) {
		len += 2; /* for "*," */
	}
	if (len == 0)
		return strdup("");

	/* omit the trailing comma, add a null-terminator.
	   it's a wash, really; len stays the same */

	char *p, *q, *s;
	p = s = vcalloc(len, sizeof(char));
	for (i = 0; i < qn->size; i++) {
		for (q = qn->parts[i].name; *q; *p++ = *q++)
			;
		*p++ = '=';
		for (q = qn->parts[i].value; *q; *p++ = *q++)
			;
		*p++ = ',';
	}
	if (qn->wildcard) {
		*p++ = '*';
		*p++ = ',';
	}
	*(--p) = '\0';

	return s;
}

int qname_match(qname_t *a, qname_t *b)
{
	/* degenerate case: * matches everything */
	if (b->size == 0 && b->wildcard)
		return 0;

	int i = 0, j = 0;
	for (;;) {
		/* do we still have components to compare? */
		if (i < a->size && j < b->size) {
			/* do the names match? */
			if (strcmp(a->parts[i].name, b->parts[j].name) == 0) {
				/* do the value match?
				   or, alternatively, is the b component a name=* wildcard? */
				if (!b->parts[j].wildcard
				 && strcmp(a->parts[i].value, b->parts[j].value) != 0) {
					return 1;
				}
				i++; j++;
				continue;
			}

			/* if not, can we skip the name in (a) because (b) has a '*'? */
			if (b->wildcard) {
				i++;
				continue;
			}
		}

		/* have we looked at all the components? */
		if (i == a->size && j == b->size)
			return 0;
		/* have we at least looked at all the (b) components?
		   only applies if (b) has a '*' to consume the rest of (a) */
		if (j == b->size && b->wildcard)
			return 0;
		return 1;
	}
}
