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
