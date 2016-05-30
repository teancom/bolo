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

#include <string.h>
#include <signal.h>
#include <assert.h>
#include <getopt.h>
#include <locale.h>
#include <math.h>

/* why ncurses defined 'OK' is beyond me */
#include <ncursesw/ncurses.h>
#undef OK

#include <pcre.h>
#if !HAVE_PCRE_FREE_STUDY
#define pcre_free_study pcre_free
#endif

#include <vigor.h>
#include <bolo.h>

#define MASK_STATE       0x01
#define MASK_TRANSITION  0x02
#define MASK_EVENT       0x04
#define MASK_RATE        0x08
#define MASK_COUNTER     0x10
#define MASK_SAMPLE      0x20

#define MASK_ALL_THE_THINGS 0x3f

#define MAX_VALUES 256

#define DEFAULT_STATUS_TOP     "bolo console v%v|[%e]|%m"
#define DEFAULT_STATUS_BOTTOM  "%n/%N (%p)  %F|pid %P|%M"

#define ME "bolo2console"

typedef struct {
	char  *name;
	char   type;
	char   unit;

	int         set[MAX_VALUES];
	long double val[MAX_VALUES];
} metric_t;

typedef struct {
	void *control;    /* SUB:  hooked up to supervisor.command; receives control messages */
	void *subscriber; /* SUB:  subscriber to bolo broadcast port (external) */
	void *tock;       /* SUB:  hooked up to scheduler.tick, for timing interrupts */

	reactor_t *reactor;

	int height; /* height of viewport */
	int width;  /* width of viewport */

	unsigned long voffset; /* index of first visible metric  */
	metric_t **metrics;
	int window; /* how often to forward the fill pointer */
	int tick;   /* incremented on every tock */

	const char *status_top;
	const char *status_bottom;

	char *endpoint; /* %e - connected bolo endpoint   */
	char *message;  /* %m - user message              */
	char *pattern;  /* %M - patten used to filter     */
	int   mask;     /* %F - enabled flag string       */

	pcre        *re;
	pcre_extra  *re_extra;

	unsigned long visible; /* %n - number of visible metrics */
	unsigned long total;   /* %N - total number of metrics   */

	int start; /* start-of-time */
	int fill;  /* fill pointer */

	int w_name;  /* how wide is the name column */
	int w_graph; /* how wide is the graph column */
} artist_t;

/*

   round-robin list

   [ 0 0 0 0 0 0 0 1 1 1 1 1 1 1 1 0 0 0 0 0 ] : set
   [ _ _ _ _ _ _ _ 4 8 1 9 2 3 0 1 _ _ _ _ _ ] : val
                     ^             ^
                     |             |
                     |             `--- fill pointer
                     |
                     `----------------- SoT pointer

   The fill pointer tells the update subroutines where
   they should place the next measured value.  All RRLs
   share a single fill pointer.  When the fill pointer
   is advanced, set[fill] will be cleared for all known
   metrics.

   The SoT (start-of-time) pointer is global; it tells
   the display routines where to start printing graphs.
   Printing will start at the SoT pointer and go to the
   `last` pointer for each RLL.
 */

static void simplify(long double *value, char *unit);
static int spread(long double min, long double max);
static void s_ingest(artist_t *a, pdu_t *pdu);
static metric_t* s_find_metric(artist_t *a, char type, const char *name);
static void s_insert_metric(artist_t *a, char type, const char *name, const char *value);
static void s_update_metric(artist_t *a, metric_t *m, const char *value);
static void s_redraw(artist_t *a);
static void s_statusline(const char *fmt, artist_t *a, int top);

static void handle_sigwinch(int sig)
{
	clear();
	/* FIXME: need a way to signal to the artist_thread that it needs to redraw NOW */
	/* FIXME: also, i'm not sure that ncurses is getting the picture that maxyx has
	          changed; may need to call something? */
}

static void simplify(long double *value, char *unit) /* {{{ */
{
	assert(value != NULL);
	assert(unit != NULL);

	*unit = ' ';
#define FACTOR(u,f) do { \
	if ((f) != 0 && *value >= (f)) { *value = *value / (f); *unit = (u); return; } \
} while (0)
	FACTOR('t', 1000UL * 1000UL * 1000UL * 1000UL);
	FACTOR('g', 1000UL * 1000UL * 1000UL);
	FACTOR('m', 1000UL * 1000UL);
	FACTOR('k', 1000UL);
#undef FACTOR
}
/* }}} */
static int spread(long double min, long double max) /* {{{ */
{
	return max - min > 10;
}
/* }}} */
static void s_ingest(artist_t *a, pdu_t *p) /* {{{ */
{
	char *name = NULL, *value = NULL, type;
	metric_t *metric;

	if (strcmp(pdu_type(p), "TRANSITION") == 0) {
		type = 'T';
		return; /* FIXME support TRANSITIONs somehow */

	} else if (strcmp(pdu_type(p), "RATE") == 0) {
		type = 'R';
		name = pdu_string(p, 2);
		value = pdu_string(p, 4);

	} else if (strcmp(pdu_type(p), "STATE") == 0) {
		type = 'A';
		return; /* FIXME support STATEs somehow */

	} else if (strcmp(pdu_type(p), "COUNTER") == 0) {
		type = 'C';
		name = pdu_string(p, 2);
		value = pdu_string(p, 3);

	} else if (strcmp(pdu_type(p), "EVENT") == 0) {
		type = 'E';
		return; /* FIXME support EVENTs somehow */

	} else if (strcmp(pdu_type(p), "SAMPLE") == 0) {
		type = 'S';
		name = pdu_string(p, 2);
		value = pdu_string(p, 7); /* mean */

	} else {
		return;
	}

	metric = s_find_metric(a, type, name);
	if (metric) {
		s_update_metric(a, metric, value);
	} else {
		if (pcre_exec(a->re, a->re_extra, name, strlen(name), 0, 0, NULL, 0) == 0) {
			s_insert_metric(a, type, name, value);
		}
	}
}
/* }}} */
static metric_t* s_find_metric(artist_t *a, char type, const char *name) /* {{{ */
{
	int i;

	for (i = 0; i < a->total; i++) {
		if (a->metrics[i]->type != type) {
			continue;
		}
		if (strcmp(a->metrics[i]->name, name) == 0) {
			return a->metrics[i];
		}
	}
	return NULL;
}
/* }}} */
static void s_insert_metric(artist_t *a, char type, const char *name, const char *value) /* {{{ */
{
	metric_t *m, **mm;
	size_t n;

	m = vmalloc(sizeof(metric_t));

	m->type = type;
	m->name = strdup(name);
	m->unit = '\0';

	mm = realloc(a->metrics, (a->total + 1) * sizeof(metric_t*));
	if (!mm) {
		return; /* FIXME: error */
	}
	mm[a->total++] = m;
	a->metrics = mm;

	s_update_metric(a, m, value);

	n = strlen(name);
	if (n > a->w_name) {
		a->w_name = n;
	}
}
/* }}} */
static void s_update_metric(artist_t *a, metric_t *m, const char *value) /* {{{ */
{
	long double v;
	char *err = NULL;

	v = strtold(value, &err);
	if (*err) {
		return; /* FIXME: error */
	}
	if (isnan(v)) {
		return; /* FIXME: error */
	}

	m->set[a->fill] = 1;
	m->val[a->fill] = v;
}
/* }}} */
static void s_redraw(artist_t *a) /* {{{ */
{
	int i;

	getmaxyx(stdscr, a->height, a->width);
	a->visible = 0;
	a->w_graph = a->width
	           - 4           /* | x  |                    metric type    */
	           - a->w_name   /*                           max name width */
	           - 12          /* |  ddd.dd u             | current value  */
	           - 23          /* |  ddd.dd u / ddd.dd u  | min/max vals   */
	           - 1           /*                           final gutter   */
	;

	for (i = 0; i < a->height - 2 && i + a->voffset < a->total; i++) {
		int j, n, b;
		long double last, min, max, lim[4];
		char u, min_u, max_u;
		char graph[MAX_VALUES * 3 + 1], dots[2048];
		memset(dots, '.', sizeof(dots) / sizeof(dots[0]));

		a->visible++;
		/* find the last set value */
		for (j = a->fill; j != a->start; j--) {
			if (a->metrics[i + a->voffset]->set[j]) {
				last = a->metrics[i + a->voffset]->val[j];
				break;
			}
		}

		/* find min and max values */
		min = max = last;
		for (j = a->fill; j != a->start; j--) {
			if (a->metrics[i + a->voffset]->set[j]) {
				if (a->metrics[i + a->voffset]->val[j] > max) {
					max = a->metrics[i + a->voffset]->val[j];
				} else if (a->metrics[i + a->voffset]->val[j] < min) {
					min = a->metrics[i + a->voffset]->val[j];
				}
			}
		}

		lim[0] = (max - min) / 5; /* 20th percentile */
		lim[1] = lim[0] * 2;      /* 40th */
		lim[2] = lim[0] * 3;      /* 60th */
		lim[3] = lim[0] * 4;      /* 80th */

		lim[0] += min;
		lim[1] += min;
		lim[2] += min;
		lim[3] += min;

		simplify(&min, &min_u);
		simplify(&max, &max_u);

		/* the simplest way to build the most recent data
		   is construct it in reverse, including the UTF-8
		   sequences, then swap the bytes around. */
		b = 0;
		n = 0;
		j = a->fill;
		while (n < a->w_graph && j != a->start) {
			if (a->metrics[i + a->voffset]->set[j]) {
				long double v = a->metrics[i + a->voffset]->val[j];

				graph[b + 2] = '\xe2';
				graph[b + 1] = '\x96';
				if (!spread(min, max)) graph[b + 0] = '\x82';
				else if (v < lim[0])   graph[b + 0] = '\x82';
				else if (v < lim[1])   graph[b + 0] = '\x83';
				else if (v < lim[2])   graph[b + 0] = '\x85';
				else if (v < lim[3])   graph[b + 0] = '\x86';
				else                   graph[b + 0] = '\x87';
				b += 3;
			} else {
				graph[b] = ' ';
				b += 1;
			}
			n++; j--;
			if (j < 0) {
				j = MAX_VALUES - 1;
			}
		}
		graph[b] = '\0';
		dots[a->w_graph - n] = '\0';
		for (n = 0, b--; b >= 0 && n < b; n++, b--) {
			char tmp = graph[n];
			graph[n] = graph[b];
			graph[b] = tmp;
		}

		simplify(&last, &u);
		mvprintw(1+i, 0, " %c  %-*s  %6.2Lf %c  %s ",
			a->metrics[i + a->voffset]->type,
			a->w_name,
			a->metrics[i + a->voffset]->name,
			last, u,
			graph);

		if (spread(min, max)) {
			attron(COLOR_PAIR(3) | A_DIM);
			printw("%s", dots);
			attroff(COLOR_PAIR(3) | A_DIM);

			printw("  %6.2Lf %c / %6.2Lf %c", min, min_u, max, max_u);
		} else {
			clrtoeol();
		}
	}

	s_statusline(a->status_top,    a, 1);
	s_statusline(a->status_bottom, a, 0);

	refresh();
}
/* }}} */
static void s_statusline(const char *fmt, artist_t *a, int top) /* {{{ */
{
	int x, y, ss = 0;
	const char *p;
	char *left, *center, *right, *b;
	size_t n, nf;

	getyx(stdscr, y, x);

	left   = calloc(a->width + 1, sizeof(char));
	center = calloc(a->width + 1, sizeof(char));
	right  = calloc(a->width + 1, sizeof(char));
	n = 0;
	if (!left || !center || !right) {
		free(left);
		free(center);
		free(right);
		return;
	}

	b = left;
	for (p = fmt; *p && n <= a->width; p++) {
		if (*p == '%') {
			p++;
			switch (*p) {
			case 0:
				/* FIXME: this is an error */
				return;

			case '%':
				*b++ = '%';
				n++;
				break;

			case 'v':
				nf = snprintf(b, a->width - n, "%s", BOLO_VERSION);
				if (nf > 0) {
					n += nf;
					b += nf;
				}
				break;

			case 'e':
				if (!a->endpoint) {
					a->endpoint = "~";
				}
				nf = snprintf(b, a->width - n, "%s", a->endpoint);
				if (nf > 0) {
					n += nf;
					b += nf;
				}
				break;

			case 'm':
				if (a->message) {
					nf = snprintf(b, a->width - n, "%s", a->message);
					if (nf > 0) {
						n += nf;
						b += nf;
					}
				}
				break;

			case 'F':
				nf = snprintf(b, a->width - n, "%c%c%c%c%c%c",
						(a->mask & MASK_TRANSITION ? 'T' : '.'),
						(a->mask & MASK_RATE       ? 'R' : '.'),
						(a->mask & MASK_STATE      ? 'A' : '.'),
						(a->mask & MASK_COUNTER    ? 'C' : '.'),
						(a->mask & MASK_EVENT      ? 'E' : '.'),
						(a->mask & MASK_SAMPLE     ? 'S' : '.'));
				if (nf > 0) {
					n += nf;
					b += nf;
				}
				break;

			case 'M':
				if (a->pattern) {
					nf = snprintf(b, a->width - n, "%s", a->pattern);
					if (nf > 0) {
						b += nf;
						n += nf;
					}
				}
				break;

			case 'n':
				nf = snprintf(b, a->width - n, "%lu", a->visible);
				if (nf > 0) {
					n += nf;
					b += nf;
				}
				break;

			case 'N':
				nf = snprintf(b, a->width - n, "%lu", a->total);
				if (nf > 0) {
					n += nf;
					b += nf;
				}
				break;

			case 'p':
				if (a->total == a->voffset + a->visible) {
					nf = snprintf(b, a->width - n, "100%%");
				} else {
					nf = snprintf(b, a->width - n, "%lu%%", (unsigned long)((a->voffset + a->visible + 0.5) / a->total * 100));
				}
				if (nf > 0) {
					n += nf;
					b += nf;
				}
				break;

			case 'P':
				nf = snprintf(b, a->width - n, "%i", getpid());
				if (nf > 0) {
					n += nf;
					b += nf;
				}
			}

		} else if (*p == '|') {
			switch (ss++) {
			case 0: /* LEFT */    b = center; n = 0; break;
			case 1: /* CENTER */  b = right;  n = 0; break;
			default: /* RIGHT */
				*b++ = '|';
				n++;
			}

		} else {
			*b++ = *p;
			n++;
		}
	}

	if (top) {
		y = 0;
	} else {
		y = a->height - 1;
	}
	attron(COLOR_PAIR(1) | A_BOLD);
	mvprintw(y, x - x, "%-*s", a->width, left);
	n = strlen(right);
	mvprintw(y, a->width - n, "%s", right);
	mvprintw(y, (int)((a->width - n - strlen(left) - strlen(center)) / 2), center);
	attroff(COLOR_PAIR(1) | A_BOLD);

	free(left);
	free(center);
	free(right);
}
/* }}} */

static int _artist_reactor(void *socket, pdu_t *pdu, void *_) /* {{{ */
{
	assert(socket != NULL);
	assert(pdu != NULL);
	assert(_ != NULL);

	artist_t *artist = (artist_t*)_;

	if (socket == artist->control) {
		if (strcmp(pdu_type(pdu), "TERMINATE") == 0)
			return VIGOR_REACTOR_HALT;

		logger(LOG_ERR, "artist thread received unrecognized [%s] PDU from control socket; ignoring",
			pdu_type(pdu));
		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == artist->tock) {
		int i, c;

		artist->tick = (artist->tick + 1) % artist->window;
		if (artist->tick == 0) {
			logger(LOG_INFO, "advancing to the next round-robin slot");
			artist->fill++;
			if (artist->fill >= MAX_VALUES) {
				artist->fill = 0;
			}
			for (i = 0; i < artist->total; i++) {
				artist->metrics[i]->set[artist->fill] = 0;
			}
			s_redraw(artist);
		}

		nodelay(stdscr, 1);
		keypad(stdscr, 1);
		switch (c = getch()) {
		case KEY_UP:
			if (artist->voffset > 0) {

				artist->voffset--;
				s_redraw(artist);
			}
			break;

		case KEY_DOWN:
			if (artist->total > artist->height
			 && artist->voffset < artist->total - (artist->height - 2)) {

				artist->voffset++;
				s_redraw(artist);
			}
			break;
		}

		return VIGOR_REACTOR_CONTINUE;
	}

	if (socket == artist->subscriber) {
		#define MATCH(x) (artist->mask & MASK_ ## x && strcmp(pdu_type(pdu), #x) == 0)
		if (MATCH(STATE)
		 || MATCH(TRANSITION)
		 || MATCH(EVENT)
		 || MATCH(RATE)
		 || MATCH(COUNTER)
		 || MATCH(SAMPLE)) {
			s_ingest(artist, pdu);
			s_redraw(artist);
		}
		return VIGOR_REACTOR_CONTINUE;
	}

	logger(LOG_ERR, "unhandled socket!");
	return VIGOR_REACTOR_HALT;
}
/* }}} */
static void* _artist_thread(void *_) /* {{{ */
{
	assert(_ != NULL);

	artist_t *artist = (artist_t*)_;

	setlocale(LC_ALL, "");
	initscr();
	signal(SIGWINCH, handle_sigwinch);
	noecho();
	curs_set(0);
	start_color();
	init_pair(0, COLOR_WHITE, COLOR_BLACK);
	init_pair(1, COLOR_YELLOW, COLOR_BLUE);
	init_pair(2, COLOR_CYAN, COLOR_BLUE);
	init_pair(3, COLOR_BLACK, COLOR_BLACK); /* FIXME: need a dim */
	s_redraw(artist);

	reactor_go(artist->reactor);
	endwin();

	logger(LOG_DEBUG, "artist: shutting down");

	zmq_close(artist->control);
	zmq_close(artist->tock);
	zmq_close(artist->subscriber);

	reactor_free(artist->reactor);

	logger(LOG_DEBUG, "artist: terminated");
	return NULL;
}
/* }}} */
int artist_thread(void *zmq, artist_t *artist) /* {{{ */
{
	assert(zmq != NULL);

	int rc;

	logger(LOG_INFO, "initializing artist thread");

	logger(LOG_DEBUG, "artist: connecting artist.control -> supervisor");
	rc = bolo_subscriber_connect_supervisor(zmq, &artist->control);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "artist: connecting artist.tock -> scheduler");
	rc = bolo_subscriber_connect_scheduler(zmq, &artist->tock);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "artist: connecting artist.subscriber <- bolo at %s", artist->endpoint);
	artist->subscriber = zmq_socket(zmq, ZMQ_SUB);
	if (!artist->subscriber)
		return -1;
	rc = zmq_setsockopt(artist->subscriber, ZMQ_SUBSCRIBE, "", 0);
	if (rc != 0)
		return rc;
	rc = vzmq_connect_af(artist->subscriber, artist->endpoint, AF_UNSPEC);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "artist: setting up artist event reactor");
	artist->reactor = reactor_new();
	if (!artist->reactor)
		return -1;

	logger(LOG_DEBUG, "artist: registering artist.control with event reactor");
	rc = reactor_set(artist->reactor, artist->control, _artist_reactor, artist);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "artist: registering artist.subscriber with event reactor");
	rc = reactor_set(artist->reactor, artist->subscriber, _artist_reactor, artist);
	if (rc != 0)
		return rc;

	logger(LOG_DEBUG, "artist: registering artist.tock with event reactor");
	rc = reactor_set(artist->reactor, artist->tock, _artist_reactor, artist);
	if (rc != 0)
		return rc;

	pthread_t tid;
	rc = pthread_create(&tid, NULL, _artist_thread, artist);
	if (rc != 0)
		return rc;

	return 0;
}
/* }}} */

int main(int argc, char **argv)
{
	int verbose = 0;

	artist_t *artist = vmalloc(sizeof(artist_t));
	artist->endpoint = strdup("tcp://127.0.0.1:2997");
	artist->pattern  = strdup(".");
	artist->window   = 60;
	artist->mask     = 0;

	struct option long_opts[] = {
		{ "help",               no_argument, NULL, 'h' },
		{ "verbose",            no_argument, NULL, 'v' },
		{ "version",            no_argument, NULL, 'V' },
		{ "endpoint",     required_argument, NULL, 'e' },
		{ "match",        required_argument, NULL, 'm' },
		{ "window",       required_argument, NULL, 'w' },
		{ "user-message", required_argument, NULL, 'u' },
		{ "transitions",        no_argument, NULL, 'T' },
		{ "rates",              no_argument, NULL, 'R' },
		{ "states",             no_argument, NULL, 'A' },
		{ "counters",           no_argument, NULL, 'C' },
		{ "events",             no_argument, NULL, 'E' },
		{ "samples",            no_argument, NULL, 'S' },
		{ 0, 0, 0, 0 },
	};
	for (;;) {
		int c, idx = 1;
		char *err = NULL;

		c = getopt_long(argc, argv, "h?Vv+e:m:w:u:TRACES", long_opts, &idx);
		if (c == -1) break;

		switch (c) {
		case 'h':
		case '?':
			printf(ME " v%s\n", BOLO_VERSION);
			printf("Usage: " ME " [-h?Vv] [-e tcp://host:port] [-TRACES] [-m PATTERN]\n\n");
			printf("Options:\n");
			printf("  -?, -h               show this help screen\n");
			printf("  -V, --version        show version information and exit\n");
			printf("  -v, --verbose        turn on debugging, to standard error\n");
			printf("  -e, --endpoint       bolo broadcast endpoint to connect to\n");
			printf("  -T, --transitions    show TRANSITION data\n");
			printf("  -R, --rates          show RATE data\n");
			printf("  -A, --states         show STATE data\n");
			printf("  -C, --counters       show COUNTER data\n");
			printf("  -E, --events         show EVENT data\n");
			printf("  -S, --samples        show SAMPLE data\n");
			printf("  -m, --match          only display things matching a PCRE pattern\n");
			printf("  -u, --user-message   custom message to display in title bar\n");
			exit(0);

		case 'V':
			printf(ME " v%s\n"
			       "Copyright (c) 2016 The Bolo Authors.  All Rights Reserved.\n",
			       BOLO_VERSION);
			exit(0);

		case 'v':
			verbose++;
			break;

		case 'e':
			free(artist->endpoint);
			artist->endpoint = strdup(optarg);
			break;

		case 'm':
			free(artist->pattern);
			artist->pattern = strdup(optarg);
			break;

		case 'u':
			free(artist->message);
			artist->message = strdup(optarg);
			break;

		case 'w':
			artist->window = strtol(optarg, &err, 10);
			if (*err) {
				fprintf(stderr, "invalid window size of '%s'\n", optarg);
				exit(1);
			}
			break;

		case 'T': artist->mask |= MASK_TRANSITION; break;
		case 'R': artist->mask |= MASK_RATE;       break;
		case 'A': artist->mask |= MASK_STATE;      break;
		case 'C': artist->mask |= MASK_COUNTER;    break;
		case 'E': artist->mask |= MASK_EVENT;      break;
		case 'S': artist->mask |= MASK_SAMPLE;     break;

		default:
			fprintf(stderr, "unhandled option flag %#02x\n", c);
			return 1;
		}
	}

	if (!artist->mask)
		artist->mask = MASK_ALL_THE_THINGS;

	log_open(ME, "console");
	if (verbose)
		log_level(LOG_INFO + verbose, NULL);
	else
		log_level(LOG_EMERG, NULL);

	logger(LOG_NOTICE, "starting up");

	const char *re_err;
	int re_off;
	artist->re = pcre_compile(artist->pattern, 0, &re_err, &re_off, NULL);
	if (!artist->re) {
		fprintf(stderr, "Bad --match pattern (%s): %s\n", artist->pattern, re_err);
		exit(1);
	}
	artist->re_extra = pcre_study(artist->re, 0, &re_err);

	artist->window *= 1000 / 100;
	artist->tick = 0;

	void *zmq = zmq_ctx_new();
	if (!zmq) {
		logger(LOG_ERR, "failed to initialize 0MQ context");
		return 3;
	}

	int rc;
	rc = bolo_subscriber_init();
	if (rc != 0) {
		logger(LOG_ERR, "failed to initialize subscriber architecture");
		exit(2);
	}

	rc = bolo_subscriber_scheduler_thread(zmq, 100);
	if (rc != 0) {
		logger(LOG_ERR, "failed to spin up scheduler thread");
		exit(2);
	}

	artist->status_top    = getenv("BOLO_CONSOLE_TITLE");
	artist->status_bottom = getenv("BOLO_CONSOLE_STATUS");
	if (!artist->status_top) {
		artist->status_top = DEFAULT_STATUS_TOP;
	}
	if (!artist->status_bottom) {
		artist->status_bottom = DEFAULT_STATUS_BOTTOM;
	}

	rc = artist_thread(zmq, artist);
	if (rc != 0) {
		logger(LOG_ERR, "failed to spin up artist thread");
		exit(2);
	}

	bolo_subscriber_supervisor(zmq);
	return 0;
}
