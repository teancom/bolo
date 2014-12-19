#include "bolo.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define LINE_BUF_SIZE 8192

#define T_KEYWORD_CONTROL    0x02
#define T_KEYWORD_NSCA_PORT  0x03
#define T_KEYWORD_LOG        0x04
#define T_KEYWORD_SAVEFILE   0x05
#define T_KEYWORD_DUMPFILES  0x06
#define T_KEYWORD_TYPE       0x07
#define T_KEYWORD_FRESHNESS  0x08
#define T_KEYWORD_STATE      0x09
#define T_KEYWORD_USE        0x0a

#define T_OPEN_BRACE         0x80
#define T_CLOSE_BRACE        0x81
#define T_STRING             0x82
#define T_NUMBER             0x83
#define T_TYPENAME           0x845

typedef struct {
	FILE *io;
	int   lineno;
	int   token;
	char  tval[LINE_BUF_SIZE];
	char  line[LINE_BUF_SIZE];
	char  raw[LINE_BUF_SIZE];
} parser_t;

static int lex(parser_t *p)
{
	char *a, *b;

	if (!*p->line) {
getline:
		if (!fgets(p->raw, LINE_BUF_SIZE, p->io))
			return 0;
		p->lineno++;

		a = p->raw;
		while (*a && isspace(*a)) a++;
		if (*a == '#')
			while (*a && *a != '\n') a++;
		while (*a && isspace(*a)) a++;
		if (!*a)
			goto getline;

		b = p->line;
		while ((*b++ = *a++));
	}
	p->token = 0;
	p->tval[0] = '\0';

#define KEYWORD(s,k) if (strcmp(a, s) == 0) p->token = T_KEYWORD_ ## k

	b = p->line;
	while (*b && isspace(*b)) b++;
	a = b;

	/* single-char sigils */
	if (*b == '{') {
		b++;
		p->token = T_OPEN_BRACE;

		while (*b && isspace(*b)) b++;
		memmove(p->line, b, strlen(b)+1);
		return 1;
	}
	if (*b == '}') {
		b++;
		p->token = T_CLOSE_BRACE;

		while (*b && isspace(*b)) b++;
		memmove(p->line, b, strlen(b)+1);
		return 1;
	}

	if (isalnum(*b) || *b == '/') {
		b++;
		while (*b && (isalnum(*b) || ispunct(*b)))
			b++;
		if (!*b || isspace(*b)) {
			*b++ = '\0';
			KEYWORD("control",   CONTROL);
			KEYWORD("nsca.port", NSCA_PORT);
			KEYWORD("log",       LOG);
			KEYWORD("savefile",  SAVEFILE);
			KEYWORD("dumpfiles", DUMPFILES);
			KEYWORD("type",      TYPE);
			KEYWORD("freshness", FRESHNESS);
			KEYWORD("state",     STATE);
			KEYWORD("use",       USE);

			if (!p->token) {
				memcpy(p->tval, p->line, b-p->line);
				p->token = T_STRING;
			}

			while (*b && isspace(*b)) b++;
			memmove(p->line, b, strlen(b)+1);
			return 1;
		}
		fprintf(stderr, "WARNING: next character ASCII %#02x unrecognized!\n", *b);
		b = a;
	}

	if (*b == ':') {
		b++;
		while (*b && (isalnum(*b) || *b == '-' || *b == '_'))
			b++;

		if (!*b || isspace(*b)) {
			*b++ = '\0';
			memcpy(p->tval, p->line, b-p->line);
			p->token = T_TYPENAME;

			while (*b && isspace(*b)) b++;
			memmove(p->line, b, strlen(b)+1);
			return 1;
		}
		b = a;
	}

	if (*b == '"') {
		b++; a = p->tval;
		while (*b && *b != '"' && *b != '\r' && *b != '\n') {
			if (*b == '\\') b++;
			*a++ = *b++;
		}
		*a = '\0';
		if (*b == '"') b++;
		else fprintf(stderr, "WARNING: unterminated string literal\n");

		p->token = T_STRING;
		while (*b && isspace(*b)) b++;
		memmove(p->line, b, strlen(b)+1);
		return 1;
	}

	fprintf(stderr, "failed to parse token:\n%s", p->line);
	return 0;
}

int configure(const char *path, server_t *s)
{
	parser_t p;
	memset(&p, 0, sizeof(p));

	p.io = fopen(path, "r");
	if (!p.io) return -1;

#define NEXT if (!lex(&p)) { fprintf(stderr, "Unexpected end of configuration file (at line %i)\n", p.lineno); goto bail; }
#define ERROR(s) fprintf(stderr, "%s (at line %i)\n", s, p.lineno); goto esyntax
#define SERVER_STRING(x) NEXT; if (p.token != T_STRING) { ERROR("Expected string value"); } \
	free(x); x = strdup(p.tval)
#define SERVER_NUMBER(x) NEXT; if (p.token != T_STRING) { ERROR("Expected string value"); } \
	x = atoi(p.tval)

	const char *default_type = NULL;
	type_t *type = NULL;
	state_t *state = NULL;

	for (;;) {
		if (!lex(&p)) break;

		switch (p.token) {
		case T_KEYWORD_CONTROL:   SERVER_STRING(s->config.stat_endpoint); break;
		case T_KEYWORD_NSCA_PORT: SERVER_NUMBER(s->config.nsca_port);     break;
		case T_KEYWORD_SAVEFILE:  SERVER_STRING(s->config.savefile);      break;
		case T_KEYWORD_DUMPFILES: SERVER_STRING(s->config.dumpfiles);     break;

		case T_KEYWORD_LOG:
			SERVER_STRING(s->config.log_level);
			SERVER_STRING(s->config.log_facility);
			break;

		case T_KEYWORD_USE:
			NEXT; if (p.token != T_TYPENAME) { ERROR("Expected a type name for `use TYPENAME` construct"); }
			default_type = strdup(p.tval);
			break;

		case T_KEYWORD_TYPE:
			NEXT; if (p.token != T_TYPENAME) { ERROR("Expected a type name for `type TYPENAME { ... }` construct"); }
			type = calloc(1, sizeof(type_t));
			type->freshness = 300;
			type->status    = WARNING;
			hash_set(&s->db.types, p.tval, type);

			NEXT; if (p.token != T_OPEN_BRACE) { ERROR("Expected open curly brace ({) in type definition"); }
			for (;;) {
				NEXT; if (p.token == T_CLOSE_BRACE) break;
				switch (p.token) {
					case T_KEYWORD_FRESHNESS:
						NEXT; if (p.token != T_STRING) { ERROR("Expected numeric value for `freshness` directive"); }
						type->freshness = atoi(p.tval);
						break;

					case T_STRING:
						     if (strcmp(p.tval, "warning")  == 0) type->status = WARNING;
						else if (strcmp(p.tval, "critical") == 0) type->status = CRITICAL;
						else if (strcmp(p.tval, "unknown")  == 0) type->status = UNKNOWN;
						else { ERROR("Unknown type attribute"); }
						NEXT; if (p.token != T_STRING) { ERROR("Expected string message in type definition"); }
						type->summary = strdup(p.tval);
						break;

					default: ERROR("Unexpected token in type definition");
				}
			}

			if (!type->summary) {
				if (type->freshness >= 3600) {
					type->summary = string("No results received for more than %i hour%s",
							type->freshness / 3600, type->freshness < 7200 ? "" : "s");
				} else if (type->freshness >= 60) {
					type->summary = string("No results received for more than %i minute%s",
							type->freshness / 60, type->freshness < 120 ? "" : "s");
				} else {
					type->summary = string("No results received for more than %i second%s",
							type->freshness, type->freshness == 1 ? "" : "s");
				}
			}
			break;

		case T_KEYWORD_STATE:
			NEXT;
			type = NULL;
			if (p.token == T_TYPENAME) {
				type = hash_get(&s->db.types, p.tval);
				NEXT;

			} else if (default_type) {
				type = hash_get(&s->db.types, default_type);

			}

			if (p.token != T_STRING) { ERROR("Expected string value for `state` declaration"); }

			if (!type) {
				fprintf(stderr, "ERROR: failed to determine type for %s\n", p.tval);
				goto bail;
			}

			state = calloc(1, sizeof(state_t));
			hash_set(&s->db.states, p.tval, state);
			state->type    = type;
			state->status  = PENDING;
			state->expiry  = type->freshness + time_s();
			state->summary = strdup("(state is pending results)");

			break;

		default:
			ERROR("Unexpected token at top-level");
		}
	}

	if (!feof(p.io))
		goto bail;

	fclose(p.io);
	return 0;

esyntax:
	fprintf(stderr, "syntax error\n");
bail:
	fprintf(stderr, "configuration failed\n");
	fclose(p.io);
	return 1;
}
