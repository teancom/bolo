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

#include <stdint.h>
#include <curl/curl.h>
#include <vigor.h>

#include <string.h>
#include <signal.h>
#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pcre.h>

#include <bolo.h>
#include <config.h>

#define ME "bolo2slack"

#define UA PACKAGE_NAME " (" ME ")/" PACKAGE_VERSION

static struct {
	char *endpoint;

	int   verbose;
	int   daemonize;

	char *pidfile;
	char *user;
	char *group;

	char *match;

	char *webhook;
	char *username;
	char *channel;
	char *avatar;

	/* not used directly by option-handling... */
	pcre        *re;
	pcre_extra  *re_extra;
	CURL *curl;
} OPTIONS = { 0 };

typedef struct {
	char *json;
	size_t len;
	size_t sent;
} request_t;

static size_t s_quotedlen(const char *s)
{
	const char *p;
	size_t n = 0;
	int esc = 0;

	p = s;
	while (*p) {
		if (!esc && *p == '"') {
			n++; /* add a backslash */
		}
		if (*p == '\n' || *p == '\t' || *p == '\r') {
			n++; /* add a backslash */
		}
		n++;
		esc = 0;
		if (*p == '\\') {
			esc = 1;
		}
		p++;
	}

	return n;
}

static size_t s_quotedcopy(char *dst, const char *src, size_t n, size_t len)
{
	const char *p;

	dst += n;
	p = src;
	while (*p && n < len) {
		if (*p == '\n') {
			p++;
			*dst++ = '\\'; n++;
			if (n == len) break;
			*dst++ = 'n'; n++;
			if (n == len) break;

		} else if (*p == '\r') {
			p++;
			*dst++ = '\\'; n++;
			if (n == len) break;
			*dst++ = 'r'; n++;
			if (n == len) break;

		} else if (*p == '\t') {
			p++;
			*dst++ = '\\'; n++;
			if (n == len) break;
			*dst++ = 't'; n++;
			if (n == len) break;

		} else if (*p == '"') {
			p++;
			*dst++ = '\\'; n++;
			if (n == len) break;
			*dst++ = '"'; n++;
			if (n == len) break;

		} else {
			*dst++ = *p++;
			n++;
		}
	}

	return n;
}

request_t *s_new_request(const char *avatar, const char *username, const char *channel, const char *text)
{
	request_t *r;
	size_t len = 1 + 14 + s_quotedlen(avatar)   + 2 +   /* {"icon_emoji":"...",    */ \
	                 12 + s_quotedlen(username) + 2 +   /*  "username":"...",      */
	                 11 + s_quotedlen(channel)  + 2 +   /*  "channel":"...",       */ \
	                  8 + s_quotedlen(text)     + 1 +   /*  "text":"..."           */ \
	             1;                                     /* }                       */
	size_t n;

	r = vmalloc(sizeof(request_t));
	r->json = vmalloc(len + 1);
	r->len  = len;
	r->sent = 0;

	n = 0;
	r->json[n++] = '{';

	memcpy(r->json + n, "\"icon_emoji\":\"", 14);
	n = s_quotedcopy(r->json, avatar, n + 14, len);

	memcpy(r->json + n, "\",\"username\":\"", 14);
	n = s_quotedcopy(r->json, username, n + 14, len);

	memcpy(r->json + n, "\",\"channel\":\"", 13);
	n = s_quotedcopy(r->json, channel, n + 13, len);

	memcpy(r->json + n, "\",\"text\":\"", 10);
	n = s_quotedcopy(r->json, text, n + 10, len);

	r->json[n++] = '\"';
	r->json[n++] = '}';

	return r;
}

static size_t s_slack_writer(void *buf, size_t each, size_t n, void *user)
{
	/* not currently handling responses */
	return n;
}

static size_t s_slack_reader(char *buf, size_t each, size_t n, void *user)
{
	request_t *r = (request_t*)user;
	if (r->len == r->sent)
		return 0;

	size_t sent = (each * n);
	if (sent > r->len - r->sent)
		sent = r->len - r->sent;

	printf(">>> %s\n", r->json);
	printf(">>> %s\n", r->json + r->sent);
	memcpy(buf, r->json + r->sent, sent);
	r->sent += sent;

	return sent;
}

static void s_notify(pdu_t *p)
{
	char *name    = pdu_string(p, 1);
	char *ts      = pdu_string(p, 2);
	char *stale   = pdu_string(p, 3);
	char *code    = pdu_string(p, 4);
	char *summary = pdu_string(p, 5);

	if (pcre_exec(OPTIONS.re, OPTIONS.re_extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
		char error[CURL_ERROR_SIZE], *message;
		request_t *r;
		struct curl_slist *headers = NULL;

		logger(LOG_INFO, "matched %s against /%s/, notifying", name, OPTIONS.match);

		message = string("%s is now *%s*\n> %s\n", name, code, summary);
		r = s_new_request(OPTIONS.avatar, OPTIONS.username, OPTIONS.channel, message);

		curl_easy_reset(OPTIONS.curl);
		headers = curl_slist_append(headers, "Content-type: application/json");
		curl_easy_setopt(OPTIONS.curl, CURLOPT_HTTPHEADER,    headers);
		curl_easy_setopt(OPTIONS.curl, CURLOPT_WRITEFUNCTION, s_slack_writer);
		curl_easy_setopt(OPTIONS.curl, CURLOPT_READFUNCTION,  s_slack_reader);
		curl_easy_setopt(OPTIONS.curl, CURLOPT_READDATA,      r);
		curl_easy_setopt(OPTIONS.curl, CURLOPT_USERAGENT,     UA);
		curl_easy_setopt(OPTIONS.curl, CURLOPT_ERRORBUFFER,   error);
		curl_easy_setopt(OPTIONS.curl, CURLOPT_URL,           OPTIONS.webhook);
		curl_easy_setopt(OPTIONS.curl, CURLOPT_POST,          1);
		curl_easy_setopt(OPTIONS.curl, CURLOPT_POSTFIELDSIZE, r->len);
		/* FIXME: free headers */

		logger(LOG_INFO, "submitting data to %s", OPTIONS.webhook);
		if (curl_easy_perform(OPTIONS.curl) != CURLE_OK) {
			logger(LOG_ERR, "failed to notify via Slack webhook <%s>: %s", OPTIONS.webhook, error);
		}
	}

	free(name);
	free(ts);
	free(stale);
	free(code);
	free(summary);
}

int main(int argc, char **argv)
{
	OPTIONS.verbose   = 0;
	OPTIONS.endpoint  = strdup("tcp://127.0.0.1:2997");
	OPTIONS.daemonize = 1;
	OPTIONS.pidfile   = strdup("/var/run/" ME ".pid");
	OPTIONS.user      = strdup("root");
	OPTIONS.group     = strdup("root");
	OPTIONS.match     = strdup(".");
	OPTIONS.avatar    = strdup(":robot_face:");
	OPTIONS.username  = strdup("bolo");

	struct option long_opts[] = {
		{ "help",             no_argument, NULL, 'h' },
		{ "version",          no_argument, NULL, 'V' },
		{ "verbose",          no_argument, NULL, 'v' },
		{ "endpoint",   required_argument, NULL, 'e' },
		{ "foreground",       no_argument, NULL, 'F' },
		{ "pidfile",    required_argument, NULL, 'p' },
		{ "user",       required_argument, NULL, 'u' },
		{ "group",      required_argument, NULL, 'g' },
		{ "match",      required_argument, NULL, 'm' },
		{ "webhook",    required_argument, NULL, 'U' },
		{ "channel",    required_argument, NULL, 'C' },
		{ "botname",    required_argument, NULL, 'N' },
		{ "avatar",     required_argument, NULL, 'A' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int idx = 1;
		int c = getopt_long(argc, argv, "h?Vv+e:Fp:u:g:m:U:C:N:A:", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf(ME " v%s\n", BOLO_VERSION);
			printf("Usage: " ME " [-h?FVv] [-e tcp://host:port]\n"
			       "                  [-l level]\n"
			       "                  [-u user] [-g group] [-p /path/to/pidfile]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -F, --foreground     don't daemonize, stay in the foreground\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -u, --user           user to run as (if daemonized)\n");
			printf("  -g, --group          group to run as (if daemonized)\n");
			printf("  -p, --pidfile        where to store the pidfile (if daemonized)\n");
			printf("  -U, --webhook        Slack webhook URL for integration\n");
			printf("  -C, --channel        channel (#channel or @user) to notify\n");
			printf("  -N, --botname        name to use for the notification robot\n");
			printf("  -A, --avatar         avatar image to use (either :emoji: or a URL)\n");
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

		case 'm':
			free(OPTIONS.match);
			OPTIONS.match = strdup(optarg);
			break;

		case 'U':
			free(OPTIONS.webhook);
			OPTIONS.webhook = strdup(optarg);
			break;

		case 'C':
			free(OPTIONS.channel);
			OPTIONS.channel = strdup(optarg);
			break;

		case 'N':
			free(OPTIONS.username);
			OPTIONS.username = strdup(optarg);
			break;

		case 'A':
			free(OPTIONS.avatar);
			OPTIONS.avatar = strdup(optarg);
			break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (!OPTIONS.channel) {
		fprintf(stderr, "Missing required --channel flag.\n");
		return 1;
	}
	if (!OPTIONS.webhook) {
		fprintf(stderr, "Missing required --webhook flag.\n");
		return 1;
	}

	if (OPTIONS.daemonize) {
		log_open(ME, "daemon");
		log_level(LOG_ERR + OPTIONS.verbose, NULL);

		mode_t um = umask(0);
		if (daemonize(OPTIONS.pidfile, OPTIONS.user, OPTIONS.group) != 0) {
			fprintf(stderr, "daemonization failed: (%i) %s\n", errno, strerror(errno));
			return 3;
		}
		umask(um);
	} else {
		log_open(ME, "console");
		log_level(LOG_INFO + OPTIONS.verbose, NULL);
	}
	logger(LOG_NOTICE, "starting up");

	const char *re_err;
	int re_off;
	OPTIONS.re = pcre_compile(OPTIONS.match, 0, &re_err, &re_off, NULL);
	if (!OPTIONS.re) {
		fprintf(stderr, "Bad --match pattern (%s): %s\n", OPTIONS.match, re_err);
		exit(1);
	}
	OPTIONS.re_extra = pcre_study(OPTIONS.re, 0, &re_err);

	logger(LOG_DEBUG, "initializing curl subsystem");
	OPTIONS.curl = curl_easy_init();
	if (!OPTIONS.curl) {
		logger(LOG_ERR, "failed to initialize curl subsystem");
		return 3;
	}

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

			if (strcmp(pdu_type(p), "TRANSITION") == 0 && pdu_size(p) == 6) {
				s_notify(p);
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
