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

#include <bolo.h>
#include <vigor.h>
#include <stdlib.h>
#include <string.h>

struct __bolo_name_part {
	char *name;
	char *value;
	int   wildcard;
};
struct __bolo_name {
	int size;
	int wildcard;
	struct __bolo_name_part* parts;
};

/***************************************************************/

static int s_sort(const void *a_, const void *b_)
{
	struct __bolo_name_part *a = (struct __bolo_name_part*)a_;
	struct __bolo_name_part *b = (struct __bolo_name_part*)b_;
	return a->name && b->name ? strcmp(a->name, b->name) : 0;
}

/***************************************************************/

bolo_name_t bolo_name_parse(const char *name)
{
	bolo_name_t qn;
	const char *a, *b;
	int i;

	qn = vmalloc(sizeof(struct __bolo_name));

	/* degenerate case */
	if (!*name)
		return qn;

	int n = 1;
	for (a = name; *a; a++)
		if (*a == ',')
			n++;

	qn->parts = vcalloc(n, sizeof(struct __bolo_name_part));
	for (i = 0, a = name; i < n;) {
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

		qn->parts[i].name = strndup(a, b - a);
		for (a = ++b; *b && *b != ','; b++)
			;
		qn->parts[i].wildcard = strncmp(a, "*", b - a) == 0 ? 1 : 0;
		qn->parts[i].value = strndup(a, b - a);
		i++;

		if (!*b)
			break;
		a = b + 1;
	}

	qsort(qn->parts, n, sizeof(struct __bolo_name_part), s_sort);
	qn->size = i;
	return qn;
}

bolo_name_t bolo_name_copy(bolo_name_t name)
{
	bolo_name_t copy = vmalloc(sizeof(struct __bolo_name));
	copy->size     = name->size;
	copy->wildcard = name->wildcard;
	copy->parts    = vcalloc(name->size, sizeof(struct __bolo_name_part));

	int i;
	for (i = 0; i < name->size; i++) {
		copy->parts[i].name     = name->parts[i].name;
		copy->parts[i].value    = name->parts[i].value;
		copy->parts[i].wildcard = name->parts[i].wildcard;
	}

	return copy;
}

char* bolo_name_string(bolo_name_t name)
{
	size_t len = 0;
	int i;

	for (i = 0; i < name->size; i++) {
		len += strlen(name->parts[i].name)  + 1 /* = */
		     + strlen(name->parts[i].value) + 1 /* , */;
	}
	if (name->wildcard) {
		len += 2; /* for "*," */
	}
	if (len == 0)
		return strdup("");

	/* omit the trailing comma, add a null-terminator.
	   it's a wash, really; len stays the same */

	char *p, *q, *s;
	p = s = vcalloc(len, sizeof(char));
	for (i = 0; i < name->size; i++) {
		for (q = name->parts[i].name; *q; *p++ = *q++)
			;
		*p++ = '=';
		for (q = name->parts[i].value; *q; *p++ = *q++)
			;
		*p++ = ',';
	}
	if (name->wildcard) {
		*p++ = '*';
		*p++ = ',';
	}
	*(--p) = '\0';

	return s;
}

int bolo_name_match(bolo_name_t a, bolo_name_t b)
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

int bolo_name_set(bolo_name_t name, const char *key, const char *value)
{
	int wild = (!value || strcmp(value, "*") == 0) ? 1 : 0;
	int i;
	for (i = 0; i < name->size; i++) {
		if (strcmp(name->parts[i].name, key) == 0) {
			free(name->parts[i].value);
			name->parts[i].value = wild ? NULL : strdup(value);
			return 0;
		}
	}

	struct __bolo_name_part* new;
	new = realloc(name->parts, sizeof(struct __bolo_name_part) * (name->size + 1));
	if (!new) {
		return -1;
	}
	name->parts = new;
	name->parts[name->size].name     = strdup(key);
	name->parts[name->size].value    = wild ? NULL : strdup(value);
	name->parts[name->size].wildcard = wild;
	name->size++;
	qsort(name->parts, name->size, sizeof(struct __bolo_name_part), s_sort);
	return 0;
}

int bolo_name_unset(bolo_name_t name, const char *key)
{
	int i;
	for (i = 0; i < name->size; i++) {
		if (strcmp(name->parts[i].name, key) == 0) {
			free(name->parts[i].name);
			free(name->parts[i].value);
			for (; i < name->size - 1; i++) {
				name->parts[i].name     = name->parts[i+1].name;
				name->parts[i].value    = name->parts[i+1].value;
				name->parts[i].wildcard = name->parts[i+1].wildcard;
			}
			name->parts[name->size - 1].name     = NULL;
			name->parts[name->size - 1].value    = NULL;
			name->parts[name->size - 1].wildcard = 0;
			name->size--;
			return 0;
		}
	}
	return 0;
}

int bolo_name_concat(bolo_name_t a, bolo_name_t b)
{
	int i;

	for (i = 0; i < b->size; i++) {
		if (bolo_name_set(a, b->parts[i].name, b->parts[i].value) != 0)
			return -1;
	}
	return 0;
}
