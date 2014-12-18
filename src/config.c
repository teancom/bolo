#include "bolo.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define LINE_BUF_SIZE 8192

#define T_KEYWORD_LISTENER   1
#define T_KEYWORD_CONTROL    2
#define T_KEYWORD_NSCA_PORT  3
#define T_KEYWORD_LOG        4
#define T_KEYWORD_SAVEFILE   5
#define T_KEYWORD_DUMPFILES  6
#define T_KEYWORD_TYPE       7
#define T_KEYWORD_FRESHNESS  8
#define T_KEYWORD_STATE      9
#define T_OPEN_BRACE        10
#define T_CLOSE_BRACE       11
#define T_STRING            12
#define T_QSTRING           13
#define T_NUMBER            14
#define T_TYPENAME          15

typedef struct {
	FILE *io;
	char *token;
	char  line[LINE_BUF_SIZE];
	char  raw[LINE_BUF_SIZE];
} parser_t;

static int lex(parser_t *p)
{
	char *a, *b;

getline:
	if (!fgets(p->raw, LINE_BUF_SIZE, p->io))
		return 0;

	a = p->raw;
	while (*a && isspace(*a)) a++;
	if (*a == '#')
		while (*a && *a != '\n') a++;
	while (*a && isspace(*a)) a++;
	if (!*a)
		goto getline;

	b = p->line;
	while (*a && *a != '\n')
		*b++ = *a++;
	*b = '\0';

	p->token = p->line;
	return 1;
}

int configure(const char *path, server_t *s, db_t *d)
{
	parser_t p;
	memset(&p, 0, sizeof(p));

	p.io = fopen(path, "r");
	if (!p.io) return -1;

	while (lex(&p)) {
		fprintf(stderr, "got a token: [%s]\n", p.token);
	}

	fclose(p.io);
	return 0;
}
