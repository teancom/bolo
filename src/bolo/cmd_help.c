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
#include <vigor.h>

int cmd_help(int off, int argc, char **argv)
{
	if (off < argc - 1) {
		execlp("man", "man", string("bolo-%s", argv[off+1]), NULL);
		return 1;
	}
	fprintf(stderr, "USAGE: bolo [-h] [-V] <command> [options]\n"
	                "\n"
	                "Options:\n"
	                "  -h, --help      Show this help screen.\n"
	                "  -V, --version   Print bolo version and exit.\n"
	                "\n"
	                "Commands:\n"
	                "\n"
	                "  aggr      Run the Bolo Aggregator (daemon) process.\n"
	                "  cache     Subscriber store-n-forward cache.\n"
	                "  forget    Instruct a remote aggregator to forget data.\n"
	                "  query     Query a Bolo Aggregator for information.\n"
	                "  send      Submit data to a Bolo Aggregator.\n");
	return 0;
}
