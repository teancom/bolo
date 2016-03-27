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

#include <stdio.h>
#include "../src/bolo.h"

int main(int argc, char **argv)
{
	/*
	    qname dump name=blah,test=this
	    qname match x=y,a=b a=b,x=y
	    qname match '*' x=y
	    qname fix b=c,a=b
	 */

	if (argc < 2) {
		fprintf(stderr, "USAGE: %s <command> [options]\n", argv[0]);
		exit(1);
	}

	if (strcmp(argv[1], "dump") == 0) {
		if (argc < 3) {
			fprintf(stderr, "USAGE: %s dump <name> [<name> ...]\n", argv[0]);
			exit(1);
		}
		int i;
		for (i = 2; i < argc; i++) {
			fprintf(stderr, "qname: %s\n", argv[i]);

			qname_t* qn = qname_parse(argv[i]);
			if (qn == NULL) {
				fprintf(stderr, "  !! ERROR: not a valid qualified name\n");
				continue;
			}

			fprintf(stderr, "  %i qname components\n", qn->size);

			int i;
			for (i = 0; i < qn->size; i++) {
				fprintf(stderr, "  - name [%s] = value [%s]\n",
						qn->parts[i].name, qn->parts[i].value);
			}
		}

	} else if (strcmp(argv[1], "match") == 0) {
		if (argc != 4) {
			fprintf(stderr, "USAGE: %s match <name> <other-name>\n", argv[0]);
			exit(1);
		}

		qname_t *a, *b;
		a = qname_parse(argv[2]);
		b = qname_parse(argv[3]);

		if (!a) fprintf(stderr, "%s: not a valid qualified name\n", argv[2]);
		if (!b) fprintf(stderr, "%s: not a valid qualified name\n", argv[3]);
		if (!a || !b) exit(1);

		if (qname_match(a, b) != 0) {
			fprintf(stdout, "no\n");
			exit(2);
		}
		fprintf(stdout, "yes\n");
		exit(0);

	} else if (strcmp(argv[1], "check") == 0) {
		if (argc < 3) {
			fprintf(stderr, "USAGE: %s check <name> [<name> ...]\n", argv[0]);
			exit(1);
		}
		int i;
		for (i = 2; i < argc; i++)
			fprintf(stdout, "%svalid\n", qname_parse(argv[i]) ? "" : "in");
		exit(0);

	} else if (strcmp(argv[1], "fix") == 0) {
		if (argc < 3) {
			fprintf(stderr, "USAGE: %s fix <name> [<name> ...]\n", argv[0]);
			exit(1);
		}
		int i;
		for (i = 2; i < argc; i++) {
			qname_t* qn = qname_parse(argv[i]);
			if (qn == NULL) {
				fprintf(stderr, "%s: not a valid qualified name\n", argv[i]);
				continue;
			}

			char *s = qname_string(qn);
			fprintf(stdout, "%s\n", s);
			free(s);
		}

	} else {
		/* FIXME: help / -h / --help */
		fprintf(stderr, "Unrecognized command '%s'\n", argv[1]);
		exit(1);
	}

	return 0;
}
