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
#include <stdio.h>
#include <string.h>

int cmd_name(int off, int argc, char **argv)
{
	/*
	    bolo name match x=y,a=b a=b,x=y
	    bolo name match '*' x=y
	    bolo name fix b=c,a=b
	    bolo name concat a,b x,y
	    bolo name add a,b x=y z=z
	    bolo name rm a,b,x=y,z=z x z
	 */

	if (argc - off < 2) {
		fprintf(stderr, "USAGE: bolo name <command> [options]\n");
		exit(1);
	}

	if (strcmp(argv[off + 1], "match") == 0) {
		if (argc - off != 4) {
			fprintf(stderr, "USAGE: bolo name match <name> <other-name>\n");
			exit(1);
		}

		bolo_name_t a, b;
		a = bolo_name_parse(argv[off + 2]);
		b = bolo_name_parse(argv[off + 3]);

		if (!a) fprintf(stderr, "%s: not a valid qualified name\n", argv[off + 2]);
		if (!b) fprintf(stderr, "%s: not a valid qualified name\n", argv[off + 3]);
		if (!a || !b) exit(1);

		if (bolo_name_match(a, b) != 0) {
			fprintf(stdout, "no\n");
			exit(2);
		}
		fprintf(stdout, "yes\n");
		exit(0);

	} else if (strcmp(argv[off + 1], "check") == 0) {
		if (argc - off < 3) {
			fprintf(stderr, "USAGE: bolo name check <name> [<name> ...]\n");
			exit(1);
		}
		int i;
		for (i = off + 2; i < argc; i++)
			fprintf(stdout, "%svalid\n", bolo_name_parse(argv[i]) ? "" : "in");
		exit(0);

	} else if (strcmp(argv[off + 1], "fix") == 0) {
		if (argc - off < 3) {
			fprintf(stderr, "USAGE: bolo name fix <name> [<name> ...]\n");
			exit(1);
		}
		int i;
		for (i = off + 2; i < argc; i++) {
			bolo_name_t name = bolo_name_parse(argv[i]);
			if (name == NULL) {
				fprintf(stderr, "%s: not a valid qualified name\n", argv[i]);
				continue;
			}

			char *s = bolo_name_string(name);
			fprintf(stdout, "%s\n", s);
			free(s);
		}

	} else if (strcmp(argv[off + 1], "concat") == 0) {
		if (argc - off < 4) {
			fprintf(stderr, "USAGE: bolo concat <name> <name> [...]\n");
			exit(1);
		}
		bolo_name_t name = bolo_name_parse(argv[off + 2]);
		if (name == NULL) {
			fprintf(stderr, "%s: not a valid qualified name\n", argv[off + 2]);
			exit(1);
		}
		int i;
		for (i = off + 3; i < argc; i++) {
			bolo_name_t plus = bolo_name_parse(argv[i]);
			if (plus == NULL) {
				fprintf(stderr, "%s: not a valid qualified name\n", argv[i]);
				continue;
			}
			if (bolo_name_concat(name, plus) != 0) {
				char *s = bolo_name_string(name);
				fprintf(stderr, "%s: failed to concat with %s\n", argv[i], s);
				free(s);
				continue;
			}
		}
		char *s = bolo_name_string(name);
		fprintf(stdout, "%s\n", s);
		free(s);

	} else if (strcmp(argv[off + 1], "set") == 0) {
		if (argc - off < 5 || (argc - off - 3) % 2 != 0) {
			fprintf(stderr, "USAGE: bolo set <name> <key> <value> [ <key> <value> ...]\n");
			exit(1);
		}
		bolo_name_t name = bolo_name_parse(argv[off + 2]);
		if (name == NULL) {
			fprintf(stderr, "%s: not a valid qualified name\n", argv[off + 2]);
			exit(1);
		}
		int i;
		for (i = off + 3; i < argc;) {
			const char *key = argv[i++];
			const char *val = argv[i++];
			if (bolo_name_set(name, key, val) != 0) {
				char *s = bolo_name_string(name);
				fprintf(stderr, "%s: failed to set key '%s' to '%s'\n", s, key, val);
				free(s);
				continue;
			}
		}
		char *s = bolo_name_string(name);
		fprintf(stdout, "%s\n", s);
		free(s);

	} else if (strcmp(argv[off + 1], "unset") == 0) {
		if (argc - off < 4) {
			fprintf(stderr, "USAGE: bolo unset <name> <key> [ <key> ...]\n");
			exit(1);
		}
		bolo_name_t name = bolo_name_parse(argv[off + 2]);
		if (name == NULL) {
			fprintf(stderr, "%s: not a valid qualified name\n", argv[off + 2]);
			exit(1);
		}
		int i;
		for (i = off + 3; i < argc; i++) {
			if (bolo_name_unset(name, argv[i]) != 0) {
				char *s = bolo_name_string(name);
				fprintf(stderr, "%s: failed to unset key '%s'\n", s, argv[i]);
				free(s);
				continue;
			}
		}
		char *s = bolo_name_string(name);
		fprintf(stdout, "%s\n", s);
		free(s);

	} else {
		fprintf(stderr, "Unrecognized command '%s'\n", argv[off + 1]);
		exit(1);
	}

	return 0;
}
