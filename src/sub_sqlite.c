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
#include <sqlite3.h>

static  int64_t TIME  = 0;
static uint64_t COUNT = 0;

/* temporary */
sqlite3 *DB = NULL;
sqlite3_stmt *GET    = NULL,
             *INSERT = NULL,
             *UPDATE = NULL;

static struct {
	char *endpoint;

	int   verbose;
	int   daemonize;

	char *pidfile;
	char *user;
	char *group;

	char *database;
} OPTIONS = { 0 };

typedef struct {
	char *name;
	char *ts;
	char *code;
	char *summary;
} sqstate_t;

typedef struct {
	char *type;
	char *ts;
	char *name;
} sqmetric_t;

static sqstate_t *s_state_parse(pdu_t *pdu)
{
	sqstate_t *st = vmalloc(sizeof(sqstate_t));
	st->name    = pdu_string(pdu, 1);
	st->ts      = pdu_string(pdu, 2);
	st->code    = pdu_string(pdu, 4);
	st->summary = pdu_string(pdu, 5);
	return st;
}

static void s_state_free(sqstate_t *st)
{
	if (st) {
		free(st->name);
		free(st->ts);
		free(st->code);
		free(st->summary);
	}
	free(st);
}

static sqmetric_t *s_metric_parse(pdu_t *pdu)
{
	sqmetric_t *m = vmalloc(sizeof(sqmetric_t));
	m->type  = strdup(pdu_type(pdu));
	m->ts    = pdu_string(pdu, 1);
	m->name  = pdu_string(pdu, 2);
	return m;
}

static void s_metric_free(sqmetric_t *m)
{
	if (m) {
		free(m->type);
		free(m->ts);
		free(m->name);
	}
	free(m);
}

static int s_insert_state(sqlite3 *db, sqstate_t *s)
{
	const char *stage = "setting up";
	sqlite3_stmt *get    = NULL,
	             *insert = NULL,
	             *update = NULL;
	int rc;

	stage = "preparing SELECT statement for `states` table";
	rc = sqlite3_prepare_v2(db,
		"SELECT id, status, first_seen FROM states "
		"WHERE name = ?",
		-1, &get, NULL);
	if (rc != SQLITE_OK)
		goto fail;

	stage = "preparing INSERT statement for `states` table";
	rc = sqlite3_prepare_v2(db,
		"INSERT INTO states "
		"(name, status, message, first_seen, last_seen) "
		"VALUES (?, ?, ?, ?, ?)",
		-1, &insert, NULL);
	if (rc != SQLITE_OK)
		goto fail;

	stage = "preparing UPDATE statement for `states` table";
	rc = sqlite3_prepare_v2(db,
		"UPDATE states SET "
			    "status = ?, "
			   "message = ?, "
			"first_seen = ?, "
			 "last_seen = ? "
		"WHERE id = ?",
		-1, &update, NULL);
	if (rc != SQLITE_OK)
		goto fail;

	/* first, try to find the pre-existing state record */
	stage = "attempting to find a pre-existing state";
	rc = sqlite3_bind_text(get, 1, s->name, -1, SQLITE_STATIC);
	if (rc != SQLITE_OK)
		goto fail;

	rc = sqlite3_step(get);
	if (rc == SQLITE_ROW) {
		/* a state exists */
		stage = "updating pre-existing state data";
		sqlite3_bind_text (update, 1, s->code,    -1, SQLITE_STATIC);
		sqlite3_bind_text (update, 2, s->summary, -1, SQLITE_STATIC);
		sqlite3_bind_int  (update, 3, strcmp(s->code, (char *)sqlite3_column_text(get, 1)) == 0
		                                ? sqlite3_column_int(get, 2)
		                                : atoi(s->ts));
		sqlite3_bind_int  (update, 4, atoi(s->ts));
		sqlite3_bind_int64(update, 5, sqlite3_column_int64(get, 0));

		rc = sqlite3_step(update);
		if (rc != SQLITE_DONE)
			goto fail;

	} else if (rc == SQLITE_DONE) {
		/* a new state */
		stage = "inserting a new state record";
		sqlite3_bind_text (insert, 1, s->name,    -1, SQLITE_STATIC);
		sqlite3_bind_text (insert, 2, s->code,    -1, SQLITE_STATIC);
		sqlite3_bind_text (insert, 3, s->summary, -1, SQLITE_STATIC);
		sqlite3_bind_int  (insert, 4, atoi(s->ts));
		sqlite3_bind_int  (insert, 5, atoi(s->ts));

		rc = sqlite3_step(insert);
		if (rc != SQLITE_DONE)
			goto fail;

	} else {
		/* an error occurred */
		goto fail;
	}

	sqlite3_finalize(get);
	sqlite3_finalize(insert);
	sqlite3_finalize(update);
	return 0;

fail:
	fprintf(stderr, "%s failed: %s\n", stage, sqlite3_errmsg(db));
	sqlite3_finalize(get);
	sqlite3_finalize(insert);
	sqlite3_finalize(update);
	return rc;
}

static int s_track_datapoint(sqlite3 *db, sqmetric_t *metric)
{
	const char *stage = "tracking datapoint";
	int rc;

	stage = "updating existing datapoint";
	sqlite3_reset(UPDATE);
	sqlite3_bind_int  (UPDATE, 1, atoi(metric->ts));
	sqlite3_bind_text (UPDATE, 2, metric->name, -1, SQLITE_STATIC);

	rc = sqlite3_step(UPDATE);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%s failed: %s\n", stage, sqlite3_errmsg(db));
		return rc;
	}

	if (sqlite3_changes(db) == 0) {
		stage = "inserting new datapoint";
		sqlite3_reset(INSERT);
		sqlite3_bind_text (INSERT, 1, metric->name, -1, SQLITE_STATIC);
		sqlite3_bind_text (INSERT, 2, metric->type, -1, SQLITE_STATIC);
		sqlite3_bind_int  (INSERT, 3, atoi(metric->ts));
		sqlite3_bind_int  (INSERT, 4, atoi(metric->ts));

		rc = sqlite3_step(INSERT);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "%s failed: %s\n", stage, sqlite3_errmsg(db));
			return rc;
		}
	}

	return 0;
}

static sqlite3* s_connect_db(const char *file)
{
	if (DB) return DB;

	sqlite3 *db;
	int rc = sqlite3_open(file, &db);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "connection failed\n");
		return NULL;
	}

	const char *stage = "setting up";

	stage = "preparing SELECT statement for `datapoints` table";
	rc = sqlite3_prepare_v2(db,
		"SELECT id FROM datapoints "
		"WHERE name = ? "
		  "AND type = ?",
		-1, &GET, NULL);
	if (rc != SQLITE_OK)
		goto fail;

	stage = "preparing INSERT statement for `datapoints` table";
	rc = sqlite3_prepare_v2(db,
		"INSERT INTO datapoints "
		"(name, type, first_seen, last_seen) "
		"VALUES (?, ?, ?, ?)",
		-1, &INSERT, NULL);
	if (rc != SQLITE_OK)
		goto fail;

	stage = "preparing UPDATE statement for `datapoints` table";
	rc = sqlite3_prepare_v2(db,
		"UPDATE datapoints "
			"SET last_seen = ? "
		"WHERE name = ?",
		-1, &UPDATE, NULL);
	if (rc != SQLITE_OK)
		goto fail;

	DB = db;
	return db;

fail:
	fprintf(stderr, "%s failed: %s\n", stage, sqlite3_errmsg(db));
	sqlite3_finalize(GET);
	sqlite3_finalize(INSERT);
	sqlite3_finalize(UPDATE);
	sqlite3_close(db);
	return NULL;
}

static void s_disconnect_db(sqlite3 *db)
{
	//sqlite3_close(db);
}

int main(int argc, char **argv)
{
	OPTIONS.verbose   = 0;
	OPTIONS.endpoint  = strdup("tcp://127.0.0.1:2997");
	OPTIONS.daemonize = 1;
	OPTIONS.pidfile   = strdup("/var/run/bolo2sqlite.pid");
	OPTIONS.user      = strdup("root");
	OPTIONS.group     = strdup("root");
	OPTIONS.database  = strdup("bolo");

	struct option long_opts[] = {
		{ "help",              no_argument, NULL, 'h' },
		{ "version",           no_argument, NULL, 'V' },
		{ "verbose",           no_argument, NULL, 'v' },
		{ "endpoint",    required_argument, NULL, 'e' },
		{ "foreground",        no_argument, NULL, 'F' },
		{ "pidfile",     required_argument, NULL, 'p' },
		{ "user",        required_argument, NULL, 'u' },
		{ "group",       required_argument, NULL, 'g' },
		{ "database",    required_argument, NULL, 'd' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:Fp:u:g:d:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf("bolo2sqlite v%s\n", BOLO_VERSION);
			printf("Usage: bolo2sqlite [-h?FVv] [-e tcp://host:port] [-d /path/to/db]\n"
			       "                   [-u user] [-g group] [-p /path/to/pidfile]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -F, --foreground     don't daemonize, stay in the foreground\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -d, --database       path of the database to use\n");
			printf("  -u, --user           user to run as (if daemonized)\n");
			printf("  -g, --group          group to run as (if daemonized)\n");
			printf("  -p, --pidfile        where to store the pidfile (if daemonized)\n");
			exit(0);

		case 'V':
			logger(LOG_DEBUG, "handling -V/--version");
			printf("bolo2sqlite v%s\n"
			       "Copyright (C) 2015 James Hunt\n",
			       BOLO_VERSION);
			exit(0);

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

		case 'd':
			free(OPTIONS.database);
			OPTIONS.database = strdup(optarg);
			break;


		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (OPTIONS.daemonize) {
		log_open("bolo2sqlite", "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			return 3;
		}
		umask(um);
	} else {
		log_open("bolo2sqlite", "console");
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

	pdu_t *p;
	logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);

	signal_handlers();
	while (!signalled()) {
		while ((p = pdu_recv(z))) {
			logger(LOG_INFO, "received a [%s] PDU", pdu_type(p));

			if (strcmp(pdu_type(p), "STATE") == 0 && pdu_size(p) == 6) {
				sqstate_t *st = s_state_parse(p);

				sqlite3 *db = s_connect_db(OPTIONS.database);
				if (db) {
					s_insert_state(db, st);
					s_disconnect_db(db);
				}

				s_state_free(st);

			} else if (strcmp(pdu_type(p), "SAMPLE")  == 0
			        || strcmp(pdu_type(p), "RATE")    == 0
			        || strcmp(pdu_type(p), "COUNTER") == 0) {
				COUNT++;

				int64_t start = time_ms();
				sqmetric_t *m = s_metric_parse(p);

				sqlite3 *db = s_connect_db(OPTIONS.database);
				if (db) {
					s_track_datapoint(db, m);
					//s_disconnect_db(db);
				}

				s_metric_free(m);
				TIME += time_ms() - start;
				fprintf(stderr, "processed %lu records in %lims (%5.2f/s)\n",
						COUNT, TIME, COUNT / (TIME / 1000.0));
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
