#include <stdio.h>
#include <bolo.h>

int cmd_version(int off, int argc, char **argv)
{
	fprintf(stdout, "bolo v%s\nCopyright (C) 2016 James Hunt\n", BOLO_VERSION);
	return 0;
}
