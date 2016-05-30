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

#include <assert.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libpq-fe.h>
#include <vigor.h>
#include <bolo.h>

#define ME "bolo2pg"

/***********************************************************/

typedef struct {
	int id;           /* simple identifying number for the thread; used in logging */

	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */

	void *queue;      /* PULL: hooked up to dispatcher.inserts; receives INSERT requests */

	reactor_t *reactor;

	PGconn *db;
	char   *dsn;
} inserter_t;

int inserter_thread(void *zmq, int id, const char *dsn);

/***********************************************************/

typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */
	void *tock;       /* SUB:  hooked up to scheduler.tick, for timing interrupts */

	reactor_t *reactor;

	PGconn *db;
	char   *dsn;
} reconciler_t;

int reconciler_thread(void *zmq, const char *dsn);

/***********************************************************/

typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *monitor;    /* PUSH: hooked up to monitor.input; relays monitoring messages */

	void *subscriber; /* SUB:  subscriber to bolo broadcast port (external) */

	void *inserts;    /* PUSH: fair-queue of inserter_t workers (for INSERT requests) */

	reactor_t *reactor;

	hash_t seen;
} dispatcher_t;

int dispatcher_thread(void *zmq, const char *endpoint);

/***********************************************************/

static int _inserter_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	inserter_t *inserter = (inserter_t*)_;

	if (socket == inserter->control) {
		if (strcmp(pdu_type(pdu), "TERMINATE") == 0)
			return VIGOR_REACTOR_HALT;

		logger(LOG_ERR, "inserter thread received unrecognized [%s] PDU from control socket; ignoring",
			pdu_type(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == inserter->queue) {
		if (strcmp(pdu_type(pdu), "STATE") == 0 && pdu_size(pdu) == 6) {

			int rc = 0;

			stopwatch_t watch;
			uint64_t spent = 0;

			STOPWATCH(&watch, spent) {
				char* params[] = {
					pdu_string(pdu, 1), /* name   */
					pdu_string(pdu, 4), /* code    */
					pdu_string(pdu, 5), /* summary */
					pdu_string(pdu, 2), /* ts      */
				};
#ifdef BOLO2PG_DEBUG
				logger(LOG_DEBUG, "inserter[%i]: INSERTING %s @%s (code=%s) %s",
						inserter->id, params[0], params[3], params[2], params[1]);
#endif
				PGresult *r = PQexecPrepared(inserter->db, "INSERT_STAGING", 4,
					(const char **)params,
					NULL,     /* text params; lengths not necessary */
					NULL, 0); /* all text formats */

				if (PQresultStatus(r) != PGRES_COMMAND_OK) {
					logger(LOG_ERR, "failed to insert into staging table ($1 = '%s', $2 = '%s', $3 = '%s', $4 = '%s'): %s",
						params[0], params[1], params[2], params[3],
						PQresultErrorMessage(r));
					rc = -1;
				}

				PQclear(r);

				free(params[0]);
				free(params[1]);
				free(params[2]);
				free(params[3]);
			}

			if (rc == 0) {
				pdu_t *perf = pdu_make("SAMPLE", 1, "insert.time.s");
				pdu_extendf(perf, "%lf", spent / 1000.);
				pdu_send_and_free(perf, inserter->monitor);

				pdu_send_and_free(pdu_make("COUNT", 1, "insert.ops"), inserter->monitor);

			} else {
				pdu_send_and_free(pdu_make("COUNT", 1, "insert.errors"), inserter->monitor);
			}

		} else {
			logger(LOG_WARNING, "dropping unknown [%s] PDU", pdu_type(pdu));
		}

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
static void * _inserter_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	inserter_t *inserter = (inserter_t*)_;

	reactor_go(inserter->reactor);

	int id = inserter->id;
	logger(LOG_DEBUG, "inserter[%i]: shutting down", id);

	zmq_close(inserter->queue);
	zmq_close(inserter->control);
	zmq_close(inserter->monitor);

	reactor_free(inserter->reactor);
	PQfinish(inserter->db);
	free(inserter->dsn);
	free(inserter);

	logger(LOG_DEBUG, "inserter[%i]: terminated", id);
	return NULL;
}
/* }}} */
int inserter_thread(void *zmq, int id, const char *dsn) /* {{{ */
{
	assert(zmq != NULL);
	assert(dsn != NULL);

	int rc;

	logger(LOG_INFO, "initializing inserter thread #%i", id);
	inserter_t *inserter = vmalloc(sizeof(inserter_t));
	inserter->id = id;
	inserter->dsn = strdup(dsn);

	logger(LOG_DEBUG, "inserter[%i]: connecting inserter.control -> supervisor", id);
	rc = bolo_subscriber_connect_supervisor(zmq, &inserter->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "inserter[%i]: connecting inserter.monitor -> monitor", id);
	rc = bolo_subscriber_connect_monitor(zmq, &inserter->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "inserter[%i]: connecting inserter.queue -> dispatcher", id);
	inserter->queue = zmq_socket(zmq, ZMQ_PULL);
	if (!inserter->queue)
		return -1;
	rc = zmq_connect(inserter->queue, "inproc://" ME "/v1/dispatcher.inserts");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "inserter[%i]: setting up inserter event reactor", id);
	inserter->reactor = reactor_new();
	if (!inserter->reactor)
		return -1;

	logger(LOG_DEBUG, "inserter[%i]: registering inserter.control with event reactor", id);
	rc = reactor_set(inserter->reactor, inserter->control, _inserter_reactor, inserter);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "inserter[%i]: registering inserter.queue with event reactor", id);
	rc = reactor_set(inserter->reactor, inserter->queue, _inserter_reactor, inserter);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "inserter[%i]: connecting to database", id);
	inserter->db = PQconnectdb(inserter->dsn);
	if (!inserter->db)
		return -1;
	if (PQstatus(inserter->db) != CONNECTION_OK) {
		logger(LOG_ERR, "inserter[%i]: connection failed: %s",
				id, PQerrorMessage(inserter->db));
		return -1;
	}

	const char *sql =
		"INSERT INTO states_staging "
		"(name, status, message, occurred_at) "
		"VALUES ($1, $2, $3, to_timestamp($4))";

	PGresult *r = PQprepare(inserter->db, "INSERT_STAGING", sql, 4, NULL);

	if (PQresultStatus(r) != PGRES_COMMAND_OK) {
		logger(LOG_ERR, "Unable to prepare SQL `%s`: %s", sql, PQresultErrorMessage(r));

		PQclear(r);
		PQfinish(inserter->db);
		return -1;
	}
	PQclear(r);

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _inserter_thread, inserter);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

/***********************************************************/

static int _reconciler_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	reconciler_t *reconciler = (reconciler_t*)_;

	if (socket == reconciler->control) {
		if (strcmp(pdu_type(pdu), "TERMINATE") == 0)
			return VIGOR_REACTOR_HALT;

		logger(LOG_ERR, "reconciler thread received unrecognized [%s] PDU from control socket; ignoring",
			pdu_type(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == reconciler->tock) {
		logger(LOG_INFO, "reconciling staged data");

		int rc = 0;

		stopwatch_t watch;
		uint64_t spent = 0;

		STOPWATCH(&watch, spent) {
			const char *sql = "SELECT reconcile()";
			PGresult *r = PQexec(reconciler->db, sql);
			if (PQresultStatus(r) != PGRES_TUPLES_OK) {
				logger(LOG_ERR, "`%s' failed\nerror: %s", sql, PQresultErrorMessage(r));
				rc = -1;
			}
			PQclear(r);
		}

		if (rc == 0) {
			pdu_t *perf = pdu_make("SAMPLE", 1, "reconcile.time.s");
			pdu_extendf(perf, "%lf", spent / 1000.);
			pdu_send_and_free(perf, reconciler->monitor);

			pdu_send_and_free(pdu_make("COUNT", 1, "reconcile.ops"), reconciler->monitor);

		} else {
			pdu_send_and_free(pdu_make("COUNT", 1, "reconcile.errors"), reconciler->monitor);
		}

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
static void * _reconciler_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	reconciler_t *reconciler = (reconciler_t*)_;

	reactor_go(reconciler->reactor);

	logger(LOG_DEBUG, "reconciler: shutting down");

	zmq_close(reconciler->control);
	zmq_close(reconciler->tock);
	zmq_close(reconciler->monitor);

	reactor_free(reconciler->reactor);
	PQfinish(reconciler->db);
	free(reconciler->dsn);
	free(reconciler);

	logger(LOG_DEBUG, "reconciler: terminated");
	return NULL;
}
/* }}} */
int reconciler_thread(void *zmq, const char *dsn) /* {{{ */
{
	assert(zmq != NULL);
	assert(dsn != NULL);

	int rc;

	logger(LOG_INFO, "initializing reconciler thread");
	reconciler_t *reconciler = vmalloc(sizeof(reconciler_t));
	reconciler->dsn = strdup(dsn);

	logger(LOG_DEBUG, "reconciler: connecting reconciler.control -> supervisor");
	rc = bolo_subscriber_connect_supervisor(zmq, &reconciler->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "reconciler: connecting reconciler.monitor -> monitor");
	rc = bolo_subscriber_connect_monitor(zmq, &reconciler->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "reconciler: connecting reconciler.tock -> scheduler");
	rc = bolo_subscriber_connect_scheduler(zmq, &reconciler->tock);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "reconciler: setting up reconciler event reactor");
	reconciler->reactor = reactor_new();
	if (!reconciler->reactor)
		return -1;

	logger(LOG_DEBUG, "reconciler: registering reconciler.control with event reactor");
	rc = reactor_set(reconciler->reactor, reconciler->control, _reconciler_reactor, reconciler);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "reconciler: registering reconciler.tock with event reactor");
	rc = reactor_set(reconciler->reactor, reconciler->tock, _reconciler_reactor, reconciler);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "reconciler: connecting to database");
	reconciler->db = PQconnectdb(reconciler->dsn);
	if (!reconciler->db)
		return -1;
	if (PQstatus(reconciler->db) != CONNECTION_OK) {
		logger(LOG_ERR, "reconciler: connection failed :%s", PQerrorMessage(reconciler->db));
		return -1;
	}

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _reconciler_thread, reconciler);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

/***********************************************************/

#define SEEN (char**)0x42
static int _dispatcher_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	dispatcher_t *dispatcher = (dispatcher_t*)_;

	if (socket == dispatcher->control) {
		if (strcmp(pdu_type(pdu), "TERMINATE") == 0)
			return VIGOR_REACTOR_HALT;

		logger(LOG_ERR, "dispatcher thread received unrecognized [%s] PDU from control socket; ignoring",
			pdu_type(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == dispatcher->subscriber) {
		stopwatch_t watch;
		uint64_t spent = 0;

		STOPWATCH(&watch, spent) {
			if (strcmp(pdu_type(pdu), "STATE") == 0 && pdu_size(pdu) == 6) {
				char *state = pdu_string(pdu, 1);
				char *code  = pdu_string(pdu, 4);
				if (strcmp(code, "OK") != 0 || !hash_get(&dispatcher->seen, state)) {
					hash_set(&dispatcher->seen, state, SEEN);
					pdu_send_and_free(pdu_dup(pdu, NULL), dispatcher->inserts);
				} else {
					pdu_send_and_free(pdu_make("COUNT", 1, "dispatch.skips"), dispatcher->monitor);
				}
				free(state);
				free(code);

			} else if (strcmp(pdu_type(pdu), "TRANSITION") == 0 && pdu_size(pdu) == 6) {
				char *state = pdu_string(pdu, 1);
				hash_set(&dispatcher->seen, state, SEEN);
				free(state);

				pdu_send_and_free(pdu_dup(pdu, "STATE"), dispatcher->inserts);

			} else {
#ifdef BOLO2PG_DEBUG
				logger(LOG_DEBUG, "dispatcher: skipping broadcast [%s] PDU (%i frames)",
						pdu_type(pdu), pdu_size(pdu));
#endif
				return VIGOR_REACTOR_CONTINUE;
			}
		}

		pdu_t *perf = pdu_make("SAMPLE", 1, "dispatch.time.s");
		pdu_extendf(perf, "%lf", spent / 1000.);
		pdu_send_and_free(perf, dispatcher->monitor);

		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
static void * _dispatcher_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	dispatcher_t *dispatcher = (dispatcher_t*)_;

	reactor_go(dispatcher->reactor);

	logger(LOG_DEBUG, "dispatcher: shutting down");

	zmq_close(dispatcher->control);
	zmq_close(dispatcher->monitor);
	zmq_close(dispatcher->subscriber);
	zmq_close(dispatcher->inserts);

	reactor_free(dispatcher->reactor);
	hash_done(&dispatcher->seen, 0);
	free(dispatcher);

	logger(LOG_DEBUG, "dispatcher: terminated");
	return NULL;
}
/* }}} */
int dispatcher_thread(void *zmq, const char *endpoint) /* {{{ */
{
	assert(zmq != NULL);
	assert(endpoint != NULL);

	int rc;

	logger(LOG_INFO, "initializing dispatcher thread");
	dispatcher_t *dispatcher = vmalloc(sizeof(dispatcher_t));

	logger(LOG_DEBUG, "dispatcher: connecting dispatcher.control -> supervisor");
	rc = bolo_subscriber_connect_supervisor(zmq, &dispatcher->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: connecting dispatcher.monitor -> monitor");
	rc = bolo_subscriber_connect_monitor(zmq, &dispatcher->monitor);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: connecting dispatcher.subscriber <- bolo at %s", endpoint);
	dispatcher->subscriber = zmq_socket(zmq, ZMQ_SUB);
	if (!dispatcher->subscriber)
		return -1;
	rc = zmq_setsockopt(dispatcher->subscriber, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;
	rc = vzmq_connect_af(dispatcher->subscriber, endpoint, AF_UNSPEC);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: binding PUSH socket dispatcher.inserts");
	dispatcher->inserts = zmq_socket(zmq, ZMQ_PUSH);
	if (!dispatcher->inserts)
		return -1;
	rc = zmq_bind(dispatcher->inserts, "inproc://" ME "/v1/dispatcher.inserts");
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: setting up dispatcher event reactor");
	dispatcher->reactor = reactor_new();
	if (!dispatcher->reactor)
		return -1;

	logger(LOG_DEBUG, "dispatcher: registering dispatcher.control with event reactor");
	rc = reactor_set(dispatcher->reactor, dispatcher->control, _dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "dispatcher: registering dispatcher.subscriber with event reactor");
	rc = reactor_set(dispatcher->reactor, dispatcher->subscriber, _dispatcher_reactor, dispatcher);
	if (rc != 0)
		return rc;

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _dispatcher_thread, dispatcher);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

/***********************************************************/

static int s_read_creds(const char *file, char **user, char **pass)
{
	FILE *io = fopen(file, "r");
	if (!io) {
		perror(file);
		return 1;
	}

	char buf[256];
	if (fgets(buf, 256, io) == NULL) {
		logger(LOG_ERR, "no credentials found");
		return 1;
	}

	char *sep = strchr(buf, ':');
	char *end = strrchr(buf, '\n');

	if (!sep || !end) {
		logger(LOG_ERR, "no credentials found");
		return 1;
	}

	*end = '\0';
	*sep++ = '\0';

	*user = strdup(buf);
	*pass = strdup(sep);
	return 0;
}

int main(int argc, char **argv)
{
	struct {
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

		int   inserters;

		char *submit_to;
		char *prefix;

	} OPTIONS = {
		.verbose   = 0,
		.endpoint  = strdup("tcp://127.0.0.1:2997"),
		.submit_to = strdup("tcp://127.0.0.1:2999"),
		.daemonize = 1,
		.pidfile   = strdup("/var/run/" ME ".pid"),
		.user      = strdup("root"),
		.group     = strdup("root"),
		.database  = strdup("bolo"),
		.host      = strdup("localhost"),
		.port      = strdup("5432"),
		.inserters = 8,
		.prefix    = NULL, /* will be set later, if needed */
	};

	struct option long_opts[] = {
		{ "help",              no_argument, NULL, 'h' },
		{ "version",           no_argument, NULL, 'V' },
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
		{ "prefix",      required_argument, NULL, 'n' },
		{ "inserters",   required_argument, NULL, 'I' },
		{ "submit",      required_argument, NULL, 'S' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:Fp:u:g:C:d:H:P:n:I:S:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf(ME " v%s\n", BOLO_VERSION);
			printf("Usage: " ME " [-h?FVv] [-e tcp://host:port] [-n PREFIX] [-S tcp://host:port]\n"
			       "               [-I #] [-H dbhost] [-P dbport] [-d dbname] [-C /path/to/creds]\n"
			       "               [-u user] [-g group] [-p /path/to/pidfile]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -F, --foreground     don't daemonize, stay in the foreground\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -n, --prefix         prefix for submitting perf metrics back\n");
			printf("  -S, --submit         bolo listener endpoint for perf metrics\n");
			printf("  -I, --inserters      how many inserter worker threads to run\n");
			printf("  -H, --host           PostgreSQL database host\n");
			printf("  -P, --port           PostgreSQL database port\n");
			printf("  -d, --database       name of the database to connect to\n");
			printf("  -C, --credentials    path to the file holding auth credentials\n");
			printf("  -u, --user           user to run as (if daemonized)\n");
			printf("  -g, --group          group to run as (if daemonized)\n");
			printf("  -p, --pidfile        where to store the pidfile (if daemonized)\n");
			exit(0);

		case 'V':
			logger(LOG_DEBUG, "handling -V/--version");
			printf(ME " v%s\n"
			       "Copyright (c) 2016 The Bolo Authors.  All Rights Reserved.\n",
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

		case 'n':
			free(OPTIONS.prefix);
			OPTIONS.prefix = strdup(optarg);
			break;

		case 'I':
			OPTIONS.inserters = strtoll(optarg, NULL, 10);
			break;

		case 'S':
			free(OPTIONS.submit_to);
			OPTIONS.submit_to = strdup(optarg);
			break;


		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (!OPTIONS.prefix) {
		char *s = fqdn();
		OPTIONS.prefix = string("%s:sys:" ME, s);
		free(s);
	}

	if (OPTIONS.daemonize) {
		log_open(ME, "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			logger(LOG_ERR, "daemonization failed: (%i) %s", errno, strerror(errno));
			return 3;
		}
		umask(um);
	} else {
		log_open(ME, "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");


	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		exit(1);
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
	free(user);
	free(pass);

	int rc;
	rc = bolo_subscriber_init();
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize subscriber architecture");
		exit(2);
	}

	rc = bolo_subscriber_monitor_thread(zmq, OPTIONS.prefix, OPTIONS.submit_to);
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize monitor thread");
		exit(2);
	}

	rc = bolo_subscriber_scheduler_thread(zmq, 5 * 1000);
	if (rc != 0) {
		logger(LOG_ERR, "failed to spin up scheduler thread");
		exit(2);
	}

	rc = bolo_subscriber_metrics(zmq,
		"COUNT",  "insert.ops",
		"COUNT",  "reconcile.ops",
		"COUNT",  "insert.errors",
		"COUNT",  "reconcile.errors",
		"COUNT",  "dispatch.skips",

		"SAMPLE", "dispatch.time.s",
		"SAMPLE", "insert.time.s",
		"SAMPLE", "reconcile.time.s",
		NULL);
	if (rc != 0) {
		logger(LOG_WARNING, "failed to provision metrics");
		/* non-fatal, but still a problem... */
	}

	rc = reconciler_thread(zmq, dsn);
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize reconciler: %s", strerror(errno));
		exit(2);
	}

	rc = dispatcher_thread(zmq, OPTIONS.endpoint);
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize dispatcher: %s", strerror(errno));
		exit(2);
	}

	int i;
	for (i = 0; i < OPTIONS.inserters; i++) {
		rc = inserter_thread(zmq, i, dsn);
		if (rc != 0) {
			logger(LOG_ERR, "failed to initialize inserter #%i: %s", i, strerror(errno));
			exit(2);
		}
	}
	free(dsn);

	bolo_subscriber_supervisor(zmq);

	free(OPTIONS.endpoint);
	free(OPTIONS.submit_to);

	free(OPTIONS.pidfile);
	free(OPTIONS.user);
	free(OPTIONS.group);

	free(OPTIONS.database);
	free(OPTIONS.host);
	free(OPTIONS.port);
	free(OPTIONS.creds);

	free(OPTIONS.prefix);

	return 0;
}
