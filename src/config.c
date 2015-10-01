/*
  Copyright 2015 James Hunt <james@jameshunt.us>

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

#define LINE_BUF_SIZE 8192

#define T_KEYWORD_CONTROLLER 0x01
#define T_KEYWORD_LISTENER   0x02
#define T_KEYWORD_LOG        0x03
#define T_KEYWORD_SAVEFILE   0x04
#define T_KEYWORD_DUMPFILES  0x05
#define T_KEYWORD_TYPE       0x06
#define T_KEYWORD_FRESHNESS  0x07
#define T_KEYWORD_STATE      0x08
#define T_KEYWORD_USE        0x09
#define T_KEYWORD_PIDFILE    0x0a
#define T_KEYWORD_USER       0x0b
#define T_KEYWORD_GROUP      0x0c
#define T_KEYWORD_BROADCAST  0x0d
#define T_KEYWORD_WINDOW     0x0e
#define T_KEYWORD_COUNTER    0x0f
#define T_KEYWORD_SAMPLE     0x10
#define T_KEYWORD_NSCAPORT   0x11
#define T_KEYWORD_KEYSFILE   0x12
#define T_KEYWORD_MAXEVENTS  0x13
#define T_KEYWORD_RATE       0x14
#define T_KEYWORD_GRACE_PERIOD 0x15

#define T_OPEN_BRACE         0x80
#define T_CLOSE_BRACE        0x81
#define T_STRING             0x82
#define T_NUMBER             0x83
#define T_TYPENAME           0x84
#define T_WINDOWNAME         0x85
#define T_MATCH              0x86
#define T_TIME               0x87

typedef struct {
	FILE       *io;
	const char *file;
	int         line;
	int         token;
	char        value[LINE_BUF_SIZE];
	char        buffer[LINE_BUF_SIZE];
	char        raw[LINE_BUF_SIZE];
} parser_t;

static int lex(parser_t *p)
{
	char *a, *b;

	if (!*p->buffer) {
getline:
		if (!fgets(p->raw, LINE_BUF_SIZE, p->io))
			return 0;
		p->line++;

		a = p->raw;
		while (*a && isspace(*a)) a++;
		if (*a == '#')
			while (*a && *a != '\n') a++;
		while (*a && isspace(*a)) a++;
		if (!*a)
			goto getline;

		b = p->buffer;
		while ((*b++ = *a++));
	}
	p->token = 0;
	p->value[0] = '\0';

#define KEYWORD(s,k) if (strcmp(a, s) == 0) p->token = T_KEYWORD_ ## k

	b = p->buffer;
	while (*b && isspace(*b)) b++;
	a = b;

	/* single-char sigils */
	if (*b == '#')
		goto getline;

	if (*b == '{') {
		b++;
		p->token = T_OPEN_BRACE;

		while (*b && isspace(*b)) b++;
		memmove(p->buffer, b, strlen(b)+1);
		return 1;
	}
	if (*b == '}') {
		b++;
		p->token = T_CLOSE_BRACE;

		while (*b && isspace(*b)) b++;
		memmove(p->buffer, b, strlen(b)+1);
		return 1;
	}

	if (isdigit(*b)) {
		while (*b && isdigit(*b)) b++;
		if (!*b || isspace(*b)) {
			memcpy(p->value, p->buffer, b-p->buffer);
			p->token = T_NUMBER;

			while (*b && isspace(*b)) b++;
			memmove(p->buffer, b, strlen(b)+1);
			return 1;
		}

		int mult = 0;
		switch (*b) {
			case 'd': mult = 86400; break;
			case 'h': mult = 3600;  break;
			case 'm': mult = 60;    break;
			case 's': mult = 1;     break;
		}
		if (mult > 0) {
			*b = '\0';
			char *n = string("%i", atoi(p->buffer) * mult);
			memcpy(p->value, n, strlen(n) + 1); free(n);
			p->token = T_TIME;

			while (*b && isspace(*b)) b++;
			memmove(p->buffer, b, strlen(b)+1);
			return 1;
		}
	}

	if (*b == 'm') {
		b++;
		if (*b == '/') {
			a = ++b;
			while (*b && !isspace(*b)) {
				if (*b == '/')  break;
				if (*b == '\\') b++;
				b++;
			}
			if (*b != '/') {
				logger(LOG_ERR, "%s:%i: unterminated pattern", p->file, p->line);
				return 0;
			}

			*b++ = '\0';
			memcpy(p->value, a, b-a);
			p->token = T_MATCH;

			while (*b && isspace(*b)) b++;
			memmove(p->buffer, b, strlen(b)+1);
			return 1;
		}
		b = a;
	}

	if (isalnum(*b) || *b == '/') {
		b++;
		while (*b && (isalnum(*b) || ispunct(*b)))
			b++;
		if (!*b || isspace(*b)) {
			*b++ = '\0';
			KEYWORD("listener",   LISTENER);
			KEYWORD("controller", CONTROLLER);
			KEYWORD("broadcast",  BROADCAST);
			KEYWORD("nsca.port",  NSCAPORT);
			KEYWORD("user",       USER);
			KEYWORD("group",      GROUP);
			KEYWORD("pidfile",    PIDFILE);
			KEYWORD("log",        LOG);
			KEYWORD("savefile",   SAVEFILE);
			KEYWORD("keysfile",   KEYSFILE);
			KEYWORD("dumpfiles",  DUMPFILES);
			KEYWORD("type",       TYPE);
			KEYWORD("window",     WINDOW);
			KEYWORD("freshness",  FRESHNESS);
			KEYWORD("state",      STATE);
			KEYWORD("counter",    COUNTER);
			KEYWORD("sample",     SAMPLE);
			KEYWORD("rate",       RATE);
			KEYWORD("use",        USE);
			KEYWORD("max.events", MAXEVENTS);
			KEYWORD("grace.period", GRACE_PERIOD);

			if (!p->token) {
				memcpy(p->value, p->buffer, b-p->buffer);
				p->token = T_STRING;
			}

			while (*b && isspace(*b)) b++;
			memmove(p->buffer, b, strlen(b)+1);
			return 1;
		}
		logger(LOG_WARNING, "%s:%i: next character (ASCII %#02x) unrecognized",
			p->file, p->line, b);
		b = a;
	}

	if (*b == ':') {
		b++;
		while (*b && (isalnum(*b) || *b == '-' || *b == '_'))
			b++;

		if (!*b || isspace(*b)) {
			*b++ = '\0';
			memcpy(p->value, p->buffer, b-p->buffer);
			p->token = T_TYPENAME;

			while (*b && isspace(*b)) b++;
			memmove(p->buffer, b, strlen(b)+1);
			return 1;
		}
		b = a;
	}

	if (*b == '@') {
		b++;
		while (*b && (isalnum(*b) || *b == '-' || *b == '_'))
			b++;

		if (!*b || isspace(*b)) {
			*b++ = '\0';
			memcpy(p->value, p->buffer, b-p->buffer);
			p->token = T_WINDOWNAME;

			while (*b && isspace(*b)) b++;
			memmove(p->buffer, b, strlen(b)+1);
			return 1;
		}
		b = a;
	}

	if (*b == '"') {
		b++; a = p->value;
		while (*b && *b != '"' && *b != '\r' && *b != '\n') {
			if (*b == '\\') b++;
			*a++ = *b++;
		}
		*a = '\0';
		if (*b == '"') b++;
		else logger(LOG_WARNING, "%s:%i: unterminated string literal", p->file, p->line);

		p->token = T_STRING;
		while (*b && isspace(*b)) b++;
		memmove(p->buffer, b, strlen(b)+1);
		return 1;
	}

	logger(LOG_ERR, "%s:%i: failed to parse '%s'", p->file, p->line, p->buffer);
	return 0;
}

int configure(const char *path, server_t *s)
{
	list_init(&s->db.state_matches);
	list_init(&s->db.counter_matches);
	list_init(&s->db.sample_matches);
	list_init(&s->db.rate_matches);
	list_init(&s->db.events);
	list_init(&s->db.anon_windows);
	memset(&s->db.states,   0, sizeof(hash_t));
	memset(&s->db.counters, 0, sizeof(hash_t));
	memset(&s->db.samples,  0, sizeof(hash_t));
	memset(&s->db.rates,    0, sizeof(hash_t));
	memset(&s->db.types,    0, sizeof(hash_t));
	memset(&s->db.windows,  0, sizeof(hash_t));

	parser_t p;
	memset(&p, 0, sizeof(p));

	p.file = path;
	p.io = fopen(path, "r");
	if (!p.io) return -1;

#define NEXT if (!lex(&p)) { logger(LOG_CRIT, "%s:%i: unexpected end of configuration\n", p.file, p.line); goto bail; }
#define ERROR(s) logger(LOG_CRIT, "%s:%i: syntax error: %s", p.file, p.line, s); goto bail
#define SERVER_STRING(x) NEXT; if (p.token != T_STRING) { ERROR("Expected string value"); } \
	free(x); x = strdup(p.value)

	type_t   *type           = NULL;
	char     *default_type   = NULL;

	window_t *win            = NULL;
	char     *default_win    = NULL;

	state_t      *state      = NULL;
	re_state_t   *re_state   = NULL;

	counter_t    *counter    = NULL;
	re_counter_t *re_counter = NULL;

	sample_t     *sample     = NULL;
	re_sample_t  *re_sample  = NULL;

	rate_t       *rate       = NULL;
	re_rate_t    *re_rate    = NULL;

	const char *re_err;
	int re_off;

	for (;;) {
		if (!lex(&p)) break;

		switch (p.token) {
		case T_KEYWORD_LISTENER:   SERVER_STRING(s->config.listener);    break;
		case T_KEYWORD_CONTROLLER: SERVER_STRING(s->config.controller);  break;
		case T_KEYWORD_BROADCAST:  SERVER_STRING(s->config.broadcast);   break;
		case T_KEYWORD_USER:       SERVER_STRING(s->config.runas_user);  break;
		case T_KEYWORD_GROUP:      SERVER_STRING(s->config.runas_group); break;
		case T_KEYWORD_PIDFILE:    SERVER_STRING(s->config.pidfile);     break;
		case T_KEYWORD_SAVEFILE:   SERVER_STRING(s->config.savefile);    break;
		case T_KEYWORD_KEYSFILE:   SERVER_STRING(s->config.keysfile);    break;

		case T_KEYWORD_DUMPFILES: /* noop */ break;

		case T_KEYWORD_GRACE_PERIOD:
			NEXT;
			s->config.grace_period = atoi(p.value);
			break;

		case T_KEYWORD_MAXEVENTS:
			NEXT;
			if      (p.token == T_NUMBER)  s->config.events_keep = EVENTS_KEEP_NUMBER;
			else if (p.token == T_TIME)    s->config.events_keep = EVENTS_KEEP_TIME;
			else { ERROR("Expected number or time spec max.events"); }
			s->config.events_max = atoi(p.value);
			break;

		case T_KEYWORD_NSCAPORT:
			NEXT;
			if (p.token != T_NUMBER) { ERROR("Expected numeric port value"); }
			s->config.nsca_port = atoi(p.value) & 0xffff;
			break;

		case T_KEYWORD_LOG:
			SERVER_STRING(s->config.log_level);
			SERVER_STRING(s->config.log_facility);
			break;

		case T_KEYWORD_USE:
			NEXT;
			switch (p.token) {
			case T_TYPENAME:
				free(default_type);
				default_type = strdup(p.value);
				break;

			case T_WINDOWNAME:
				free(default_win);
				default_win = strdup(p.value);
				break;

			default:
				ERROR("Expected a type name for `use TYPENAME` construct");
				break;
			}
			break;

		case T_KEYWORD_WINDOW:
			NEXT; if (p.token != T_WINDOWNAME) { ERROR("Expected a window name for `window WINDOWNAME SECONDS` construct"); }
			win = calloc(1, sizeof(window_t));
			win->name = strdup(p.value);
			hash_set(&s->db.windows, p.value, win);
			NEXT; if (p.token != T_NUMBER) { ERROR("Expected numeric value for `window` directive"); }
			win->time = atoi(p.value);
			break;

		case T_KEYWORD_TYPE:
			NEXT; if (p.token != T_TYPENAME) { ERROR("Expected a type name for `type TYPENAME { ... }` construct"); }
			type = calloc(1, sizeof(type_t));
			type->name      = strdup(p.value);
			type->freshness = 300;
			type->status    = WARNING;
			hash_set(&s->db.types, p.value, type);

			NEXT; if (p.token != T_OPEN_BRACE) { ERROR("Expected open curly brace ({) in type definition"); }
			for (;;) {
				NEXT; if (p.token == T_CLOSE_BRACE) break;
				switch (p.token) {
					case T_KEYWORD_FRESHNESS:
						NEXT; if (p.token != T_NUMBER) { ERROR("Expected numeric value for `freshness` directive"); }
						type->freshness = atoi(p.value);
						break;

					case T_STRING:
						     if (strcmp(p.value, "warning")  == 0) type->status = WARNING;
						else if (strcmp(p.value, "critical") == 0) type->status = CRITICAL;
						else if (strcmp(p.value, "unknown")  == 0) type->status = UNKNOWN;
						else { ERROR("Unknown type attribute"); }
						NEXT; if (p.token != T_STRING) { ERROR("Expected string message in type definition"); }
						type->summary = strdup(p.value);
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
				type = hash_get(&s->db.types, p.value);
				NEXT;

			} else if (default_type) {
				type = hash_get(&s->db.types, default_type);
			}

			if (p.token == T_STRING) {
				if (!type) {
					logger(LOG_ERR, "%s:%i: failed to determine state type for '%s'", p.file, p.line, p.value);
					goto bail;
				}

				state = calloc(1, sizeof(state_t));
				hash_set(&s->db.states, p.value, state);
				state->name    = strdup(p.value);
				state->type    = type;
				state->status  = PENDING;
				state->expiry  = type->freshness + time_s();
				state->summary = strdup("(state is pending results)");

			} else if (p.token == T_MATCH) {
				if (!type) {
					logger(LOG_ERR, "%s:%i: failed to determine state type for /%s/", p.file, p.line, p.value);
					goto bail;
				}

				re_state = calloc(1, sizeof(re_state_t));
				re_state->type = type;
				re_state->re = pcre_compile(p.value, 0, &re_err, &re_off, NULL);
				if (!re_state->re) {
					logger(LOG_ERR, "%s:%i: failed to compile pattern /%s/: %s", p.file, p.line, p.value, re_err);
					goto bail;
				}

				re_state->re_extra = pcre_study(re_state->re, 0, &re_err);
				list_push(&s->db.state_matches, &re_state->l);

			} else {
				ERROR("Expected name or regex match pattern for `state` declaration");
			}

			break;

		case T_KEYWORD_COUNTER:
			NEXT;
			win = NULL;
			if (p.token == T_WINDOWNAME) {
				win = hash_get(&s->db.windows, p.value);
				NEXT;

			} else if (p.token == T_NUMBER) {
				/* anonymous window */
				win = calloc(1, sizeof(window_t));
				win->time = atoi(p.value);
				list_push(&s->db.anon_windows, &win->anon);
				NEXT;

			} else if (default_win) {
				win = hash_get(&s->db.windows, default_win);
			}

			if (p.token == T_STRING) {
				if (!win) {
					logger(LOG_ERR, "%s:%i: failed to determine window for counter '%s'",
						p.file, p.line, p.value);
					goto bail;
				}

				counter = calloc(1, sizeof(counter_t));
				hash_set(&s->db.counters, p.value, counter);
				counter->name   = strdup(p.value);
				counter->window = win;
				counter->value  = 0;

			} else if (p.token == T_MATCH) {
				if (!win) {
					logger(LOG_ERR, "%s:%i: failed to determine window for counter /%s/",
						p.file, p.line, p.value);
					goto bail;
				}

				re_counter = calloc(1, sizeof(re_counter_t));
				re_counter->window = win;
				re_counter->re = pcre_compile(p.value, 0, &re_err, &re_off, NULL);
				if (!re_counter->re) {
					logger(LOG_ERR, "%s:%i: failed to compile pattern /%s/: %s", p.file, p.line, p.value, re_err);
					goto bail;
				}

				re_counter->re_extra = pcre_study(re_counter->re, 0, &re_err);
				list_push(&s->db.counter_matches, &re_counter->l);

			} else {
				ERROR("Expected string value for `counter` declaration");
			}

			break;

		case T_KEYWORD_SAMPLE:
			NEXT;
			win = NULL;
			if (p.token == T_WINDOWNAME) {
				win = hash_get(&s->db.windows, p.value);
				NEXT;

			} else if (p.token == T_NUMBER) {
				/* anonymous window */
				win = calloc(1, sizeof(window_t));
				win->time = atoi(p.value);
				list_push(&s->db.anon_windows, &win->anon);
				NEXT;

			} else if (default_win) {
				win = hash_get(&s->db.windows, default_win);
			}

			if (p.token == T_STRING) {
				if (!win) {
					logger(LOG_ERR, "%s:%i: failed to determine window for sample '%s'",
						p.file, p.line, p.value);
					goto bail;
				}

				sample = calloc(1, sizeof(sample_t));
				hash_set(&s->db.samples, p.value, sample);
				sample->name   = strdup(p.value);
				sample->window = win;
				sample->n = 0;

			} else if (p.token == T_MATCH) {
				if (!win) {
					logger(LOG_ERR, "%s:%i: failed to determine window for sample /%s/",
						p.file, p.line, p.value);
					goto bail;
				}

				re_sample = calloc(1, sizeof(re_sample_t));
				re_sample->window = win;
				re_sample->re = pcre_compile(p.value, 0, &re_err, &re_off, NULL);
				if (!re_sample->re) {
					logger(LOG_ERR, "%s:%i: failed to compile pattern /%s/: %s", p.file, p.line, p.value, re_err);
					goto bail;
				}

				re_sample->re_extra = pcre_study(re_sample->re, 0, &re_err);
				list_push(&s->db.sample_matches, &re_sample->l);

			} else {
				ERROR("Expected string value for `sample` declaration");
			}

			break;

		case T_KEYWORD_RATE:
			NEXT;
			win = NULL;
			if (p.token == T_WINDOWNAME) {
				win = hash_get(&s->db.windows, p.value);
				NEXT;

			} else if (p.token == T_NUMBER) {
				/* anonymous window */
				win = calloc(1, sizeof(window_t));
				win->time = atoi(p.value);
				list_push(&s->db.anon_windows, &win->anon);
				NEXT;

			} else if (default_win) {
				win = hash_get(&s->db.windows, default_win);
			}

			if (p.token == T_STRING) {
				if (!win) {
					logger(LOG_ERR, "%s:%i: failed to determine window for rate '%s'",
						p.file, p.line, p.value);
					goto bail;
				}

				rate = calloc(1, sizeof(rate_t));
				hash_set(&s->db.rates, p.value, rate);
				rate->name   = strdup(p.value);
				rate->window = win;

			} else if (p.token == T_MATCH) {
				if (!win) {
					logger(LOG_ERR, "%s:%i: failed to determine window for rate /%s/",
						p.file, p.line, p.value);
					goto bail;
				}

				re_rate = calloc(1, sizeof(re_rate_t));
				re_rate->window = win;
				re_rate->re = pcre_compile(p.value, 0, &re_err, &re_off, NULL);
				if (!re_rate->re) {
					logger(LOG_ERR, "%s:%i: failed to compile pattern /%s/: %s", p.file, p.line, p.value, re_err);
					goto bail;
				}

				re_rate->re_extra = pcre_study(re_rate->re, 0, &re_err);
				list_push(&s->db.rate_matches, &re_rate->l);

			} else {
				ERROR("Expected string value for `rate` declaration");
			}

			break;

		default:
			logger(LOG_ERR, "%s:%i: unexpected token '%s' found at top-level",
				p.file, p.line, p.value);
			goto bail;
		}
	}

	if (!feof(p.io))
		goto bail;

	free(default_win);
	free(default_type);
	fclose(p.io);
	return 0;

bail:
	free(default_win);
	free(default_type);
	fclose(p.io);
	return 1;
}

int deconfigure(server_t *s)
{
	char *name;

	state_t *state;
	for_each_key_value(&s->db.states, name, state) {
		free(state->name);
		free(state->summary);
		free(state);
	}
	hash_done(&s->db.states, 0);

	sample_t *sample;
	for_each_key_value(&s->db.samples, name, sample) {
		free(sample->name);
		free(sample);
	}
	hash_done(&s->db.samples, 0);

	counter_t *counter;
	for_each_key_value(&s->db.counters, name, counter) {
		free(counter->name);
		free(counter);
	}
	hash_done(&s->db.counters, 0);

	rate_t *rate;
	for_each_key_value(&s->db.rates, name, rate) {
		free(rate->name);
		free(rate);
	}
	hash_done(&s->db.rates, 0);

	type_t *type;
	for_each_key_value(&s->db.types, name, type) {
		free(type->name);
		free(type->summary);
		free(type);
	}
	hash_done(&s->db.types, 0);

	window_t *window, *wtmp;
	for_each_object_safe(window, wtmp, &s->db.anon_windows, anon) {
		free(window);
	}
	for_each_key_value(&s->db.windows, name, window) {
		free(window->name);
		free(window);
	}
	hash_done(&s->db.windows, 0);

	re_rate_t *rrate, *rrate_tmp;
	for_each_object_safe(rrate, rrate_tmp, &s->db.rate_matches, l) {
		pcre_free_study(rrate->re_extra);
		pcre_free(rrate->re);
		free(rrate);
	}

	re_state_t *rstate, *rstate_tmp;
	for_each_object_safe(rstate, rstate_tmp, &s->db.state_matches, l) {
		pcre_free_study(rstate->re_extra);
		pcre_free(rstate->re);
		free(rstate);
	}

	re_sample_t *rsample, *rsample_tmp;
	for_each_object_safe(rsample, rsample_tmp, &s->db.sample_matches, l) {
		pcre_free_study(rsample->re_extra);
		pcre_free(rsample->re);
		free(rsample);
	}

	re_counter_t *rcounter, *rcounter_tmp;
	for_each_object_safe(rcounter, rcounter_tmp, &s->db.counter_matches, l) {
		pcre_free_study(rcounter->re_extra);
		pcre_free(rcounter->re);
		free(rcounter);
	}

	hash_done(&s->keys, 1);

	free(s->config.listener);     s->config.listener     = NULL;
	free(s->config.controller);   s->config.controller   = NULL;
	free(s->config.broadcast);    s->config.broadcast   = NULL;
	free(s->config.pidfile);      s->config.pidfile      = NULL;
	free(s->config.runas_user);   s->config.runas_user   = NULL;
	free(s->config.runas_group);  s->config.runas_group  = NULL;
	free(s->config.savefile);     s->config.savefile     = NULL;
	free(s->config.keysfile);     s->config.keysfile     = NULL;
	free(s->config.log_level);    s->config.log_level    = NULL;
	free(s->config.log_facility); s->config.log_facility = NULL;

	return 0;
}
