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

#include <stdint.h>
#include <vigor.h>

#include <string.h>
#include <signal.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pcre.h>

#include <bolo.h>
#include <bolo/subscriber.h>

#define TYPE_SYSLOG 1
#define TYPE_FILE   2

static struct {
	char *endpoint;

	int   verbose;
	int   daemonize;

	int   type;
	char *logfile;
	char *facility;
	char *ident;
	int   level;

	char *match;

	char *pidfile;
	char *user;
	char *group;

	int   all;

	/* not used directly by option-handling... */
	pcre        *re;
	pcre_extra  *re_extra;
	FILE        *logfd;

	int          reopen;
} OPTIONS = { 0 };

static void s_log(pdu_t *p)
{
	char *name    = pdu_string(p, 1);
	char *ts      = pdu_string(p, 2);
	char *stale   = pdu_string(p, 3);
	char *code    = pdu_string(p, 4);
	char *summary = pdu_string(p, 5);

	if (OPTIONS.reopen && OPTIONS.type == TYPE_FILE) {
		logger(LOG_INFO, "Reopening %s, in response to SIGHUP", OPTIONS.logfile);

		fclose(OPTIONS.logfd);
		OPTIONS.logfd = fopen(OPTIONS.logfile, "a");

		if (!OPTIONS.logfd) {
			logger(LOG_EMERG, "Unable to reopen log file: %s", strerror(errno));
			return;
		}

		OPTIONS.reopen = 0;
	}

	if (pcre_exec(OPTIONS.re, OPTIONS.re_extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
		logger(LOG_INFO, "matched %s against /%s/, logging", name, OPTIONS.match);

		if (OPTIONS.type == TYPE_SYSLOG) {
			logger(LOG_EMERG, "%s: %s %s %s %s %s", pdu_type(p), ts, stale, name, code, summary);
		} else {
			fprintf(OPTIONS.logfd, "[%s] %s: %s %s %s %s\n", ts, pdu_type(p), stale, name, code, summary);
			fflush(OPTIONS.logfd);
		}
	}

	free(name);
	free(ts);
	free(stale);
	free(code);
	free(summary);
}

void sighup_handler(int sig)
{
	OPTIONS.reopen = 1;
}

int main(int argc, char **argv)
{
	OPTIONS.verbose   = 0;
	OPTIONS.endpoint  = strdup("tcp://127.0.0.1:2997");
	OPTIONS.daemonize = 1;
	OPTIONS.type      = TYPE_SYSLOG;
	OPTIONS.logfile   = NULL;
	OPTIONS.facility  = strdup("daemon");
	OPTIONS.match     = strdup(".");
	OPTIONS.ident     = strdup("bolo2log");
	OPTIONS.level     = LOG_ALERT;
	OPTIONS.pidfile   = strdup("/var/run/bolo2log.pid");
	OPTIONS.user      = strdup("root");
	OPTIONS.group     = strdup("root");
	OPTIONS.all       = 0;

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "version",          no_argument, NULL, 'V' },
		{ "verbose",          no_argument, NULL, 'v' },
		{ "endpoint",   required_argument, NULL, 'e' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "pidfile",    required_argument, NULL, 'p' },
		{ "user",       required_argument, NULL, 'u' },
		{ "group",      required_argument, NULL, 'g' },
		{ "logfile",    required_argument, NULL, 'L' },
		{ "facility",   required_argument, NULL, 'S' },
		{ "match",      required_argument, NULL, 'm' },
		{ "ident",      required_argument, NULL, 'I' },
		{ "level",      required_argument, NULL, 'l' },
		{ "all",              no_argument, NULL, 'A' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:Fp:u:g:L:S:m:I:l:A", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf("bolo2log v%s\n", BOLO_VERSION);
			printf("Usage: bolo2log [-h?FVv] [-e tcp://host:port]\n"
			       "                [-L /log/file] [-S facility] [-I ident] [-l level] [-A] [-m PATTERN]\n"
			       "                [-u user] [-g group] [-p /path/to/pidfile]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -F, --foreground     don't daemonize, stay in the foreground\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -m, --match          only log things matching a PCRE pattern\n");
			printf("  -L, --logfile        dump broadcast data to a file directly\n");
			printf("  -S, --facility       dump broadcast data to this syslog facility\n");
			printf("  -l, --level          log syslog messages at this log level\n");
			printf("  -I, --ident          syslog identity to use with -S\n");
			printf("  -u, --user           user to run as (if daemonized)\n");
			printf("  -g, --group          group to run as (if daemonized)\n");
			printf("  -p, --pidfile        where to store the pidfile (if daemonized)\n");
			exit(0);

		case 'V':
			logger(LOG_DEBUG, "handling -V/--version");
			printf("bolo2pg v%s\n"
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

		case 'L':
			free(OPTIONS.logfile);
			OPTIONS.logfile = strdup(optarg);
			OPTIONS.type = TYPE_FILE;
			break;

		case 'S':
			free(OPTIONS.facility);
			OPTIONS.facility = strdup(optarg);
			OPTIONS.type = TYPE_SYSLOG;
			break;

		case 'm':
			free(OPTIONS.match);
			OPTIONS.match = strdup(optarg);
			break;

		case 'I':
			free(OPTIONS.ident);
			OPTIONS.ident = strdup(optarg);
			OPTIONS.type = TYPE_SYSLOG;
			break;

		case 'l':
			OPTIONS.level = log_level_number(optarg);
			OPTIONS.type = TYPE_SYSLOG;
			break;

		case 'A':
			OPTIONS.all = 1;
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (OPTIONS.daemonize) {
		log_open(OPTIONS.ident, OPTIONS.facility);
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			return 3;
		}
		umask(um);
	} else {
		log_open(OPTIONS.ident, "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");

	if (signal(SIGHUP, sighup_handler) == SIG_ERR) {
		logger(LOG_ERR, "Failed to install SIGHUP signal handler: %s", strerror(errno));
		exit(1);
	}

	if (OPTIONS.type == TYPE_FILE) {
		logger(LOG_INFO, "Opening %s for appending", OPTIONS.logfile);
		OPTIONS.logfd = fopen(OPTIONS.logfile, "a");
		if (!OPTIONS.logfd) {
			logger(LOG_ERR, "Unable to open %s for appending: %s", OPTIONS.logfile, strerror(errno));
			exit(1);
		}
	}

	const char *re_err;
	int re_off;
	OPTIONS.re = pcre_compile(OPTIONS.match, 0, &re_err, &re_off, NULL);
	if (!OPTIONS.re) {
		fprintf(stderr, "Bad --match pattern (%s): %s\n", OPTIONS.match, re_err);
		exit(1);
	}
	OPTIONS.re_extra = pcre_study(OPTIONS.re, 0, &re_err);

	logger(LOG_DEBUG, "allocating 0MQ context");
	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		return 3;
	}
	logger(LOG_DEBUG, "allocating 0MQ SUB socket to talk to %s", OPTIONS.endpoint);
	void *z = zmq_socket(zmq, ZMQ_SUB);
	if (!z) {
		logger(LOG_ERR, "failed to create a SUB socket");
		return 3;
	}
	logger(LOG_DEBUG, "setting subscriber filter");
	if (zmq_setsockopt(z, ZMQ_SUBSCRIBE, "", 0) != 0) {
		logger(LOG_ERR, "failed to set subscriber filter");
		return 3;
	}
	logger(LOG_DEBUG, "connecting to %s", OPTIONS.endpoint);
	if (vzmq_connect(z, OPTIONS.endpoint) != 0) {
		logger(LOG_ERR, "failed to connect to %s", OPTIONS.endpoint);
		return 3;
	}

	pdu_t *p;
	logger(LOG_INFO, "waiting for a PDU from %s", OPTIONS.endpoint);

	signal_handlers();
	while (!signalled()) {
		while ((p = pdu_recv(z))) {
			logger(LOG_INFO, "received a [%s] PDU of %i frames", pdu_type(p), pdu_size(p));

			if (OPTIONS.all && strcmp(pdu_type(p), "STATE") == 0 && pdu_size(p) == 6) {
				s_log(p);
			}

			if (strcmp(pdu_type(p), "TRANSITION") == 0 && pdu_size(p) == 6) {
				s_log(p);
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
