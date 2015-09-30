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

pdu_t *parse_state_pdu(int argc, char **argv, const char *ts)
{
	if (argc < 3)
		return NULL;

	int status = UNKNOWN;
	char *code = argv[1];
	if (strcasecmp(code, "0")    == 0
	 || strcasecmp(code, "ok")   == 0
	 || strcasecmp(code, "okay") == 0)
		status = OK;

	else
	if (strcasecmp(code, "1")  == 0
	 || strcasecmp(code, "warn")    == 0
	 || strcasecmp(code, "warning") == 0)
		status = WARNING;

	else
	if (strcasecmp(code, "2")        == 0
	 || strcasecmp(code, "crit")     == 0
	 || strcasecmp(code, "critical") == 0)
		status = CRITICAL;

	else
		status = UNKNOWN;

	strings_t *parts = strings_new(argv + 2);
	char *msg  = strings_join(parts, " ");
	strings_free(parts);

	pdu_t *pdu = pdu_make("STATE", 0);
	if (ts) pdu_extendf(pdu, "%s", ts);
	else    pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", argv[0]);
	pdu_extendf(pdu, "%u", status);
	pdu_extendf(pdu, "%s", msg);
	free(msg);

	return pdu;
}

pdu_t *parse_counter_pdu(int argc, char **argv, const char *ts)
{
	if (argc < 1)
		return NULL;

	int incr = 1;
	if (argc == 2 && argv[1])
		incr = atoi(argv[1]);

	pdu_t *pdu = pdu_make("COUNTER", 0);
	if (ts) pdu_extendf(pdu, "%s", ts);
	else    pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", argv[0]);
	pdu_extendf(pdu, "%u", incr);

	return pdu;
}

pdu_t *parse_sample_pdu(int argc, char **argv, const char *ts)
{
	if (argc < 2)
		return NULL;

	pdu_t *pdu = pdu_make("SAMPLE", 0);
	if (ts) pdu_extendf(pdu, "%s", ts);
	else    pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", argv[0]);

	int i;
	for (i = 1; i < argc; i++)
		pdu_extendf(pdu, "%s", argv[i]);

	return pdu;
}

pdu_t *parse_rate_pdu(int argc, char **argv, const char *ts)
{
	if (argc < 2)
		return NULL;

	pdu_t *pdu = pdu_make("RATE", 0);
	if (ts) pdu_extendf(pdu, "%s", ts);
	else    pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", argv[0]);
	pdu_extendf(pdu, "%s", argv[1]);

	return pdu;
}

pdu_t *parse_setkeys_pdu(int argc, char **argv)
{
	if (argc < 1)
		return NULL;

	pdu_t *pdu = pdu_make("SET.KEYS", 0);
	int i;
	char *k, *v;

	for (i = 0; i < argc; i++) {
		k = v = argv[i];
		while (*v && *v != '=') v++;
		if (*v) *v++ = '\0';
		else     v = "1";

		pdu_extendf(pdu, "%s", k);
		pdu_extendf(pdu, "%s", v);
	}

	return pdu;
}

pdu_t *parse_event_pdu(int argc, char **argv, const char *ts)
{
	if (argc < 1)
		return NULL;

	pdu_t *pdu = pdu_make("EVENT", 0);
	if (ts) pdu_extendf(pdu, "%s", ts);
	else    pdu_extendf(pdu, "%i", time_s());

	pdu_extendf(pdu, "%s", argv[0]);
	char *extra = "";
	if (argc >= 2) {
		strings_t *sl = strings_new(argv + 1);
		extra = strings_join(sl, " ");
		strings_free(sl);
	}
	pdu_extendf(pdu, "%s", extra);
	free(extra);

	return pdu;
}

pdu_t *stream_pdu(const char *line)
{
	pdu_t *pdu = NULL;

	while (*line && isspace(*line)) line++;
	if (!*line) return NULL;

	strings_t *l = strings_split(line, strlen(line), " ", SPLIT_NORMAL);
	if (strcasecmp(l->strings[0], "STATE") == 0) {
		pdu = parse_state_pdu(l->num - 2, l->strings + 2, l->strings[1]);

	} else if (strcasecmp(l->strings[0], "COUNTER") == 0) {
		pdu = parse_counter_pdu(l->num - 2, l->strings + 2, l->strings[1]);

	} else if (strcasecmp(l->strings[0], "SAMPLE") == 0) {
		pdu = parse_sample_pdu(l->num - 2, l->strings + 2, l->strings[1]);

	} else if (strcasecmp(l->strings[0], "RATE") == 0) {
		pdu = parse_rate_pdu(l->num - 2, l->strings + 2, l->strings[1]);

	} else if (strcasecmp(l->strings[0], "KEY") == 0) {
		pdu = parse_setkeys_pdu(l->num - 1, l->strings + 1);

	} else if (strcasecmp(l->strings[0], "EVENT") == 0) {
		pdu = parse_event_pdu(l->num - 2, l->strings + 2, l->strings[1]);
	}

	strings_free(l);
	return pdu;
}

pdu_t *state_pdu(const char *name, int status, const char *msg)
{
	pdu_t *pdu = pdu_make("STATE", 0);
	pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", name);
	pdu_extendf(pdu, "%u", status);
	pdu_extendf(pdu, "%s", msg);
	return pdu;
}

pdu_t *counter_pdu(const char *name, unsigned int value)
{
	pdu_t *pdu = pdu_make("COUNTER", 0);
	pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", name);
	pdu_extendf(pdu, "%u", value);
	return pdu;
}

static pdu_t *_vsample_pdu(const char *name, int n, va_list ap)
{
	pdu_t *pdu = pdu_make("SAMPLE", 0);
	pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", name);

	int i;
	for (i = 0; i < n; i++)
		pdu_extendf(pdu, "%lf", va_arg(ap, double));
	return pdu;
}

pdu_t *sample_pdu(const char *name, int n, ...)
{
	va_list ap;
	va_start(ap, n);
	pdu_t *pdu = _vsample_pdu(name, n, ap);
	va_end(ap);

	return pdu;
}

pdu_t *rate_pdu(const char *name, unsigned long value)
{
	pdu_t *pdu = pdu_make("RATE", 0);
	pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", name);
	pdu_extendf(pdu, "%lu", value);
	return pdu;
}

static pdu_t* _vsetkeys_pdu(int n, va_list ap)
{
	pdu_t *pdu = pdu_make("SET.KEYS", 0);

	int i;
	char *k, *v;
	for (i = 0; i < n; i++) {
		k = v = strdup(va_arg(ap, const char*));
		while (*v && *v != '=') v++;
		if (*v) *v++ = '\0';
		else     v   = "1";

		pdu_extendf(pdu, "%s", k);
		pdu_extendf(pdu, "%s", v);
		free(k);
	}

	return pdu;
}

pdu_t *setkeys_pdu(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	pdu_t *pdu = _vsetkeys_pdu(n, ap);
	va_end(ap);
	return pdu;
}

pdu_t *event_pdu(const char *name, const char *extra)
{
	pdu_t *pdu = pdu_make("EVENT", 0);
	pdu_extendf(pdu, "%i", time_s());
	pdu_extendf(pdu, "%s", name);
	pdu_extendf(pdu, "%s", extra ? extra : "");
	return pdu;
}
