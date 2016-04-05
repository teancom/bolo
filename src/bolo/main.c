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
#include <string.h>
#include <bolo.h>

int cmd_aggregator(int i, int argc, char **argv);
int cmd_cache(int i, int argc, char **argv);
int cmd_forget(int i, int argc, char **argv);
int cmd_help(int i, int argc, char **argv);
int cmd_name(int i, int argc, char **argv);
int cmd_query(int i, int argc, char **argv);
int cmd_send(int i, int argc, char **argv);
int cmd_spy(int i, int argc, char **argv);
int cmd_version(int i, int argc, char **argv);

int main(int argc, char **argv)
{
	int run_help = 0;
	int run_version = 0;

	char proc[256];
	char path[256];
	pid_t pid = getpid();
	sprintf(proc, "/proc/%d/exe", pid);
	if (readlink(proc, path, 256) == -1) {
		fprintf(stderr, "failed to read execution path from /proc\n");
		return 1;
	}

	if (strcmp(path, "/usr/sbin/bolo") == 0)
		return cmd_aggregator(argc, argc, argv);

	int i;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-h") == 0
		 || strcmp(argv[i], "-?") == 0
		 || strcmp(argv[i], "--help") == 0) {
			run_help = 1;
			continue;
		}
		if (strcmp(argv[i], "-V") == 0
		 || strcmp(argv[i], "--version") == 0) {
			run_version = 1;
			continue;
		}
		if (argv[i][0] == '-') {
			fprintf(stderr, "Invalid option `%s'\n", argv[i]);
			return 1;
		}
		break;
	}

	if (run_help)    return cmd_help(i, argc, argv);
	if (run_version) return cmd_version(i, argc, argv);
	if (i == argc)   return cmd_help(i, argc, argv);

	if (strcmp(argv[i], "help") == 0)
		return cmd_help(i, argc, argv);

	if (strcmp(argv[i], "version") == 0)
		return cmd_version(i, argc, argv);

	if (strcmp(argv[i], "aggr") == 0
	 || strcmp(argv[i], "aggregator") == 0)
		return cmd_aggregator(i, argc, argv);

	if (strcmp(argv[i], "cache") == 0)
		return cmd_cache(i, argc, argv);

	if (strcmp(argv[i], "forget") == 0)
		return cmd_forget(i, argc, argv);

	if (strcmp(argv[i], "name") == 0)
		return cmd_name(i, argc, argv);

	if (strcmp(argv[i], "query") == 0)
		return cmd_query(i, argc, argv);

	if (strcmp(argv[i], "send") == 0)
		return cmd_send(i, argc, argv);

	if (strcmp(argv[i], "spy") == 0)
		return cmd_spy(i, argc, argv);

	fprintf(stderr, "Unrecognized command `%s'.  See bolo --help.\n", argv[i]);
	return 1;
}
