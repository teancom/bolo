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
#include <getopt.h>
#include <postgresql/libpq-fe.h>

static struct {
	char *endpoint;

	int   verbose;
	int   daemonize;

	char *pidfile;
	char *user;
	char *group;

	char *database;
	char *host;
	char *port;
	char *creds;
} OPTIONS = { 0 };

typedef struct {
	char *name;
	char *ts;
	char *code;
	char *summary;
} pgstate_t;

static pgstate_t *s_state_parse(pdu_t *pdu)
{
	pgstate_t *st = vmalloc(sizeof(pgstate_t));
	st->name    = pdu_string(pdu, 1);
	st->ts      = pdu_string(pdu, 2);
	st->code    = pdu_string(pdu, 4);
	st->summary = pdu_string(pdu, 5);
	return st;
}

static void s_state_free(pgstate_t *st)
{
	if (st) {
		free(st->name);
		free(st->ts);
		free(st->code);
		free(st->summary);
	}
	free(st);
}

static int s_insert_state(PGconn *db, pgstate_t *s)
{
	const char *params[4];
	params[0] = s->name;
	params[1] = s->code;
	params[2] = s->summary;
	params[3] = s->ts;

	const char *sql =
		"INSERT INTO states_staging "
		"(name, status, message, occurred_at) "
		"VALUES ($1, $2, $3, to_timestamp($4))";

	PGresult *r = PQexecParams(db, sql, 4,
		NULL,     /* autodetect param types */
		params,
		NULL,     /* text params; lengths not necessary */
		NULL, 0); /* all text formats */

	if (PQresultStatus(r) != PGRES_COMMAND_OK) {
		fprintf(stderr, "`%s' with $1 = '%s', $2 = '%s', $3 = '%s', $4 = '%s' failed\nerror: %s",
			sql, params[0], params[1], params[2], params[3],
			PQresultErrorMessage(r));
		return -1;
	}

	return 0;
}

static int s_reconcile(PGconn *db)
{
	const char *sql = "SELECT reconcile()";
	PGresult *r = PQexec(db, sql);
	if (PQresultStatus(r) != PGRES_TUPLES_OK) {
		fprintf(stderr, "`%s' failed\nerror: %s", sql, PQresultErrorMessage(r));
		return -1;
	}

	return 0;
}

static int s_read_creds(const char *file, char **user, char **pass)
{
	FILE *io = fopen(file, "r");
	if (!io) {
		perror(file);
		return 1;
	}

	char buf[256];
	if (fgets(buf, 256, io) == NULL) {
		fprintf(stderr, "no credentials found");
		return 1;
	}

	char *sep = strchr(buf, ':');
	char *end = strrchr(buf, '\n');

	if (!sep || !end) {
		fprintf(stderr, "no credentials found");
		return 1;
	}

	*end = '\0';
	*sep++ = '\0';

	*user = strdup(buf);
	*pass = strdup(sep);
	return 0;
}

static PGconn* s_connect_db(const char *dsn)
{
	PGconn *db = PQconnectdb(dsn);
	if (!db) {
		fprintf(stderr, "connection failed\n");
		return NULL;
	}
	if (PQstatus(db) != CONNECTION_OK) {
		PQfinish(db);
		fprintf(stderr, "connection failed\n");
		return NULL;
	}

	return db;
}

static void s_disconnect_db(PGconn *db)
{
	PQfinish(db);
}

int main(int argc, char **argv)
{
	OPTIONS.verbose   = 0;
	OPTIONS.endpoint  = strdup("tcp://127.0.0.1:2997");
	OPTIONS.daemonize = 1;
	OPTIONS.pidfile   = strdup("/var/run/bolo2pg.pid");
	OPTIONS.user      = strdup("root");
	OPTIONS.group     = strdup("root");
	OPTIONS.database  = strdup("bolo");
	OPTIONS.host      = strdup("localhost");
	OPTIONS.port      = strdup("5432");

	struct option long_opts[] = {
		{ "help",              no_argument, NULL, 'h' },
		{ "verbose",           no_argument, NULL, 'v' },
		{ "endpoint",    required_argument, NULL, 'e' },
		{ "foreground",        no_argument, NULL, 'F' },
		{ "pidfile",     required_argument, NULL, 'p' },
		{ "user",        required_argument, NULL, 'u' },
		{ "group",       required_argument, NULL, 'g' },
		{ "credentials", required_argument, NULL, 'C' },
		{ "database",    required_argument, NULL, 'd' },
		{ "host",        required_argument, NULL, 'H' },
		{ "port",        required_argument, NULL, 'P' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?v+e:Fp:u:g:C:d:H:P:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			break;

		case 'v':
			OPTIONS.verbose++;
			break;

		case 'e':
			free(OPTIONS.endpoint);
			OPTIONS.endpoint = strdup(optarg);
			break;

		case 'F':
			OPTIONS.daemonize = 0;
			break;

		case 'p':
			free(OPTIONS.pidfile);
			OPTIONS.pidfile = strdup(optarg);
			break;

		case 'u':
			free(OPTIONS.user);
			OPTIONS.user = strdup(optarg);
			break;

		case 'g':
			free(OPTIONS.group);
			OPTIONS.group = strdup(optarg);
			break;

		case 'C':
			free(OPTIONS.creds);
			OPTIONS.creds = strdup(optarg);
			break;

		case 'd':
			free(OPTIONS.database);
			OPTIONS.database = strdup(optarg);
			break;

		case 'H':
			free(OPTIONS.host);
			OPTIONS.host = strdup(optarg);
			break;

		case 'P':
			free(OPTIONS.port);
			OPTIONS.port = strdup(optarg);
			break;


		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (OPTIONS.daemonize) {
		log_open("bolo2pg", "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			return 3;
		}
		umask(um);
	} else {
		log_open("bolo2pg", "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");

	logger(LOG_DEBUG, "allocating 0MQ context\n");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context\n");
		return 3;
	}
	logger(LOG_DEBUG, "allocating 0MQ SUB socket to talk to %s\n", OPTIONS.endpoint);
	void *z = zmq_socket(zmq, ZMQ_SUB);
	if (!z) {
		logger(LOG_ERR, "failed to create a SUB socket\n");
		return 3;
	}
	logger(LOG_DEBUG, "setting subscriber filter\n");
	if (zmq_setsockopt(z, ZMQ_SUBSCRIBE, "STATE", 0) != 0) {
		logger(LOG_ERR, "failed to set subscriber filter\n");
		return 3;
	}
	logger(LOG_DEBUG, "connecting to %s\n", OPTIONS.endpoint);
	if (vx_vzmq_connect(z, OPTIONS.endpoint) != 0) {
		logger(LOG_ERR, "failed to connect to %s\n", OPTIONS.endpoint);
		return 3;
	}

	char *user = NULL;
	char *pass = NULL;
	if (OPTIONS.creds && s_read_creds(OPTIONS.creds, &user, &pass) != 0)
		exit(1);

	char *dsn;
	if (user && pass)
		dsn = string("host=%s port=%s dbname=%s user=%s password=%s",
			OPTIONS.host, OPTIONS.port, OPTIONS.database, user, pass);
	else
		dsn = string("host=%s port=%s dbname=%s",
			OPTIONS.host, OPTIONS.port, OPTIONS.database);

	pdu_t *p;
	logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);

	signal_handlers();
	while (!signalled()) {
		while ((p = pdu_recv(z))) {
			logger(LOG_INFO, "received a [%s] PDU", pdu_type(p));

			if (strcmp(pdu_type(p), "STATE") == 0 && pdu_size(p) == 6) {
				pgstate_t *st = s_state_parse(p);

				PGconn *db = s_connect_db(dsn);
				if (db) {
					s_insert_state(db, st);
					s_reconcile(db);
					s_disconnect_db(db);
				}

				s_state_free(st);
			}

			pdu_free(p);
			logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);
		}
	}

	logger(LOG_INFO, "shutting down");
	vzmq_shutdown(z, 0);
	zmq_ctx_destroy(zmq);
	return 0;
}
