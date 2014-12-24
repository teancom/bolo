#include "bolo.h"
#include <arpa/inet.h>

#define RECORD_TYPE_MASK  0x000f
#define RECORD_TYPE_STATE    0x1
#define RECORD_TYPE_COUNTER  0x2
#define RECORD_TYPE_SAMPLE   0x3

#define binf_record_type(b) ((b)->flags & RECORD_TYPE_MASK)

static uint64_t htonll(uint64_t x)
{
	union {
		uint64_t u64;
		uint32_t u32[2];
	} h, n;

	h.u64 = x;
	n.u32[0] = htonl(h.u32[1]);
	n.u32[1] = htonl(h.u32[0]);
	return n.u64;
}

static uint64_t ntohll(uint64_t x)
{
	union {
		uint64_t u64;
		uint32_t u32[2];
	} h, n;

	n.u64 = x;
	h.u32[0] = ntohl(n.u32[1]);
	h.u32[1] = ntohl(n.u32[0]);
	return h.u64;
}

typedef struct PACKED {
	uint32_t  magic;
	uint16_t  version;
	uint16_t  flags;
	uint32_t  timestamp;
	uint32_t  count;
} binf_header_t;

typedef struct PACKED {
	uint16_t  len;
	uint16_t  flags;
} binf_record_t;

typedef struct PACKED {
	uint32_t  last_seen;
	 uint8_t  status;
	 uint8_t  stale;
} binf_state_t;

typedef struct PACKED {
	uint32_t  last_seen;
	uint64_t  value;
} binf_counter_t;

typedef struct PACKED {
	uint32_t  last_seen;
	uint64_t  n;
	double    min;
	double    max;
	double    sum;
	double    mean;
	double    mean_;
	double    var;
	double    var_;
} binf_sample_t;

typedef struct {
	server_t *server;
	void     *listener;
	void     *broadcast;
} kernel_t;

static inline const char *statstr(uint8_t s)
{
	static const char *names[] = { "OK", "WARNING", "CRITICAL", "UNKNOWN" };
	return names[s > 3 ? 3 : s];
}

static int write_record(int fd, ssize_t *len, uint8_t type, void *_)
{
	binf_record_t record;
	union {
		binf_state_t   state;
		binf_counter_t counter;
		binf_sample_t  sample;
	} body;
	union {
		void      *unknown;
		state_t   *state;
		counter_t *counter;
		sample_t  *sample;
	} payload;
	size_t n, want, so_far = 0;
	const char *s;

	payload.unknown = _;

	record.len   = sizeof(record);
	record.flags = type;
	switch (type) {
	case RECORD_TYPE_STATE:
		record.len += sizeof(body.state);
		record.len += strlen(payload.state->name)    + 1;
		record.len += strlen(payload.state->summary) + 1;
		break;

	case RECORD_TYPE_COUNTER:
		record.len += sizeof(body.counter);
		record.len += strlen(payload.counter->name) + 1;
		break;

	case RECORD_TYPE_SAMPLE:
		record.len += sizeof(body.sample);
		record.len += strlen(payload.sample->name) + 1;
		break;

	default:
		return -1;
	}

	if (len)
		*len = record.len;

	record.len   = htons(record.len);
	record.flags = htons(type);

	n = write(fd, &record, sizeof(record));
	so_far += n;
	if (n != sizeof(record))
		return n;

	switch (type) {
	case RECORD_TYPE_STATE:
		body.state.last_seen = htonl(payload.state->last_seen);
		body.state.status    = payload.state->status;
		body.state.stale     = payload.state->stale;

		n = write(fd, &body.state, want = sizeof(body.state));
		so_far += n;
		if (n != want) logger(LOG_CRIT, "wanted %i / got %i\n", want, n);
		if (n != want)
			return so_far;

		s = payload.state->name;
		n = write(fd, s, want = strlen(s) + 1);
		so_far += n;
		if (n != want) logger(LOG_CRIT, "wanted %i / got %i\n", want, n);
		if (n != want)
			return so_far;

		s = payload.state->summary;
		n = write(fd, s, want = strlen(s) + 1);
		so_far += n;
		if (n != want) logger(LOG_CRIT, "wanted %i / got %i\n", want, n);
		if (n != want)
			return so_far;

		break;

	case RECORD_TYPE_COUNTER:
		body.counter.last_seen = htonl(payload.counter->last_seen);
		body.counter.value     = htonll(payload.counter->value);

		n = write(fd, &body.counter, want = sizeof(body.counter));
		so_far += n;
		if (n != want)
			return so_far;

		s = payload.counter->name;
		n = write(fd, s, want = strlen(s) + 1);
		so_far += n;
		if (n != want)
			return so_far;

		break;

	case RECORD_TYPE_SAMPLE:
		body.sample.last_seen = htonl(payload.sample->last_seen);
		body.sample.min       = htonl(payload.sample->min);
		body.sample.max       = htonl(payload.sample->max);
		body.sample.sum       = htonl(payload.sample->sum);
		body.sample.mean      = htonl(payload.sample->mean);
		body.sample.mean_     = htonl(payload.sample->mean_);
		body.sample.var       = htonl(payload.sample->var);
		body.sample.var_      = htonl(payload.sample->var_);

		n = write(fd, &body.sample, sizeof(body.sample));
		so_far += n;
		if (n != sizeof(body.sample))
			return so_far;

		s = payload.sample->name;
		n = write(fd, s, want = strlen(s) + 1);
		so_far += n;
		if (n != want)
			return so_far;

		break;

	default:
		return -1;
	}

	return 0;
}

static int read_record(int fd, uint8_t *type, void **r)
{
	binf_record_t record;
	union {
		binf_state_t   state;
		binf_counter_t counter;
		binf_sample_t  sample;
	} body;
	union {
		void      *unknown;
		state_t   *state;
		counter_t *counter;
		sample_t  *sample;
	} payload;
	size_t n, want;
	char buf[4096], *p;

	n = read(fd, &record, sizeof(record));
	if (n != sizeof(record))
		return 1;

	record.len   = ntohs(record.len);
	record.flags = ntohs(record.flags);
	if (type)
		*type = binf_record_type(&record);

	switch (binf_record_type(&record)) {
	case RECORD_TYPE_STATE:
		payload.state = calloc(1, sizeof(state_t));

		n = read(fd, &body.state, want = sizeof(body.state));
		if (n != want)
			return 1;

		payload.state->last_seen = ntohl(body.state.last_seen);
		payload.state->status    = body.state.status;
		payload.state->stale     = body.state.stale;

		want = record.len - sizeof(record) - sizeof(body.state);
		if (want > 4095)
			return 1;
		n = read(fd, buf, want);
		if (n != want)
			return 1;
		buf[n] = '\0';
		for (p = buf; *p++; );
		if (p - buf + 1 == want)
			return 1;
		payload.state->name    = strdup(buf);
		payload.state->summary = strdup(p);

		*r = payload.state;
		return 0;

	case RECORD_TYPE_COUNTER:
		payload.counter = calloc(1, sizeof(counter_t));

		n = read(fd, &body.counter, want = sizeof(body.counter));
		if (n != want)
			return 1;

		payload.counter->last_seen = ntohl(body.counter.last_seen);
		payload.counter->value     = ntohll(body.counter.value);

		want = record.len - sizeof(record) - sizeof(body.counter);
		if (want > 4095)
			return 1;
		n = read(fd, buf, want);
		if (n != want)
			return 1;
		buf[n] = '\0';
		payload.counter->name = strdup(buf);

		*r = payload.counter;
		return 0;

	case RECORD_TYPE_SAMPLE:
		payload.sample = calloc(1, sizeof(sample_t));

		n = read(fd, &body.sample, want = sizeof(body.sample));
		if (n != want)
			return 1;

		payload.sample->last_seen = ntohl(body.sample.last_seen);
		payload.sample->n         = ntohl(body.sample.n);
		payload.sample->min       = ntohl(body.sample.min);
		payload.sample->max       = ntohl(body.sample.max);
		payload.sample->sum       = ntohl(body.sample.sum);
		payload.sample->mean      = ntohl(body.sample.mean);
		payload.sample->mean_     = ntohl(body.sample.mean_);
		payload.sample->var       = ntohl(body.sample.var);
		payload.sample->var_      = ntohl(body.sample.var_);

		want = record.len - sizeof(record) - sizeof(body.sample);
		if (want > 4095)
			return 1;
		n = read(fd, buf, want);
		if (n != want)
			return 1;
		buf[n] = '\0';
		payload.sample->name = strdup(buf);

		*r = payload.sample;
		return 0;

	default:
		return 1;
	}

	return 1;
}

static int save_state(db_t *db, const char *file)
{
	binf_header_t header;

	state_t   *state;
	counter_t *counter;
	sample_t  *sample;

	char *name;
	int i, n;
	ssize_t len;

	int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0640);
	if (fd < 0) {
		logger(LOG_ERR, "kernel failed to open save file %s for writing: %s",
				file, strerror(errno));
		return -1;
	}

	logger(LOG_NOTICE, "saving db state to %s", file);

	memset(&header, 0, sizeof(header));
	memcpy(&header.magic, "BOLO", 4);
	header.version   = htons(1);
	header.flags     = 0;
	header.timestamp = htonl((uint32_t)time_s());

	header.count = 0;
	for_each_key_value(&db->states,   name, state)   header.count++;
	for_each_key_value(&db->counters, name, counter) header.count++;
	for_each_key_value(&db->samples,  name, sample)  header.count++;
	header.count = htonl(header.count);

	logger(LOG_INFO, "writing %i bytes for the binary file header", sizeof(header));
	n = write(fd, &header, sizeof(header));
	if (n != sizeof(header)) {
		logger(LOG_ERR, "only wrote %i of %i bytes for the savefile header; %s is probably corrupt now",
			n, sizeof(header), file);
		close(fd);
		return -1;
	}
	i = 1;
	for_each_key_value(&db->states, name, state) {
		n = write_record(fd, &len, RECORD_TYPE_STATE, state);
		if (n != 0) {
			logger(LOG_ERR, "only wrote %i of %i bytes for state record #%i (%s); savefile %s is probably corrupt now",
				n, len, i, name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i of %i bytes for counter record #%i (%s)", n, len, i, name);
		i++;
	}
	for_each_key_value(&db->counters, name, counter) {
		n = write_record(fd, &len, RECORD_TYPE_COUNTER, counter);
		if (n != 0) {
			logger(LOG_ERR, "only wrote %i of %i bytes for counter record #%i (%s); savefile %s is probably corrupt now",
				n, len, i, name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i of %i bytes for counter record #%i (%s)", n, len, i, name);
		i++;
	}
	for_each_key_value(&db->samples, name, sample) {
		n = write_record(fd, &len, RECORD_TYPE_SAMPLE, sample);
		if (n != 0) {
			logger(LOG_ERR, "only wrote %i of %i bytes for samples record #%i (%s); savefile %s is probably corrupt now",
				n, len, i, name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i of %i bytes for samples record #%i (%s)", n, len, i, name);
		i++;
	}
	logger(LOG_INFO, "done writing savefile %s", file);
	close(fd);
	return 0;
}

static int read_state(db_t *db, const char *file)
{
	binf_header_t header;
	union {
		void      *unknown;
		state_t   *state;
		counter_t *counter;
		sample_t  *sample;
	} payload, found;
	int i, n;
	uint8_t type;

	int fd = open(file, O_RDONLY);
	if (fd < 0) {
		logger(LOG_ERR, "kernel failed to open %s for reading: %s",
			file, strerror(errno));
		return -1;
	}

	logger(LOG_NOTICE, "reading db state from savefile %s", file);
	n = read(fd, &header, sizeof(header));
	if (n < 4 || memcmp(&header.magic, "BOLO", 4) != 0) {
		logger(LOG_ERR, "%s does not seem to be a bolo savefile", file);
		close(fd);
		return -1;
	}

	if (n != sizeof(header)) {
		logger(LOG_ERR, "%s: invalid header size %i (expected %u)",
			file, n, sizeof(header));
		close(fd);
		return -1;
	}

	header.version   = ntohs(header.version);
	header.flags     = ntohs(header.flags);
	header.timestamp = ntohl(header.timestamp);
	header.count     = ntohl(header.count);

	logger(LOG_NOTICE, "%s is a v%i database, dated %lu, and contains %u records",
			file, header.version, header.timestamp, header.count);

	if (header.version != 1) {
		logger(LOG_ERR, "%s is a v%u savefile; this version of bolo only supports v1 files",
			file, header.version);
		close(fd);
		return -1;
	}

	for (i = 1; i <= header.count; i++) {
		logger(LOG_INFO, "reading record #%i from savefile", i);

		if (read_record(fd, &type, &payload.unknown) != 0) {
			logger(LOG_ERR, "%s: failed to read all of record #%i", file, i);
			close(fd);
			return -1;
		}

		switch (type) {
		case RECORD_TYPE_STATE:
			if ((found.state = hash_get(&db->states, payload.state->name)) != NULL) {
				free(found.state->summary);
				found.state->summary   = payload.state->summary;
				found.state->last_seen = payload.state->last_seen;
				found.state->status    = payload.state->status;
				found.state->stale     = payload.state->stale;

			} else {
				logger(LOG_INFO, "state %s not found in configuration, skipping", payload.state->name);
				free(payload.state->summary);
			}

			free(payload.state->name);
			free(payload.state);
			payload.state = NULL;
			break;

		case RECORD_TYPE_COUNTER:
			if ((found.counter = hash_get(&db->counters, payload.counter->name)) != NULL) {
				found.counter->last_seen = payload.counter->last_seen;
				found.counter->value     = payload.counter->value;

			} else {
				logger(LOG_INFO, "counter %s not found in configuration, skipping", payload.counter->name);
			}

			free(payload.counter->name);
			free(payload.counter);
			payload.counter = NULL;
			break;

		case RECORD_TYPE_SAMPLE:
			if ((found.sample = hash_get(&db->samples, payload.sample->name)) != NULL) {
				found.sample->last_seen = payload.sample->last_seen;
				found.sample->n         = payload.sample->n;
				found.sample->min       = payload.sample->min;
				found.sample->max       = payload.sample->max;
				found.sample->sum       = payload.sample->sum;
				found.sample->mean      = payload.sample->mean;
				found.sample->mean_     = payload.sample->mean_;
				found.sample->var       = payload.sample->var;
				found.sample->var_      = payload.sample->var_;

			} else {
				logger(LOG_INFO, "sample %s not found in configuration, skipping", payload.sample->name);
			}

			free(payload.sample->name);
			free(payload.sample);
			payload.sample = NULL;
			break;

		default:
			logger(LOG_ERR, "unknown record type %02x found!", type);
			close(fd);
			return 1;
		}
	}
	logger(LOG_INFO, "done reading savefile %s", file);
	close(fd);
	return 0;
}

static void check_freshness(db_t *db)
{
	state_t *state;
	char *k;
	uint32_t now = time_s();

	logger(LOG_INFO, "checking freshness");
	for_each_key_value(&db->states, k, state) {
		if (state->expiry > now) continue;

		logger(LOG_INFO, "state %s is stale; marking", k);
		state->stale   = 1;
		state->expiry  = now + state->type->freshness;
		state->status  = state->type->status;
		free(state->summary);
		state->summary = strdup(state->type->summary);
	}
}

static void dump(db_t *db, const char *file)
{
	if (!file) {
		logger(LOG_ERR, "kernel cannot dump state; no dump file name provided");
		return;
	}
	int fd = open(file, O_WRONLY|O_CREAT|O_EXCL, 0644);
	if (fd < 0) {
		logger(LOG_ERR, "kernel cannot dump state; unable to open %s for writing: %s",
				file, strerror(errno));
		return;
	}

	FILE *io = fdopen(fd, "w");
	if (!io) {
		logger(LOG_ERR, "kernel cannot dump state; failed to open %s for writing: %s",
				file, strerror(errno));
		return;
	}

	logger(LOG_NOTICE, "dumping state data, in YAML format, to %s", file);
	fprintf(io, "---\n");
	fprintf(io, "# generated by bolo\n");

	char *name; state_t *state;
	for_each_key_value(&db->states, name, state) {
		fprintf(io, "%s:\n", name);
		fprintf(io, "  status:    %s\n", statstr(state->status));
		fprintf(io, "  message:   %s\n", state->summary);
		fprintf(io, "  last_seen: %i\n", state->last_seen);
		fprintf(io, "  fresh:     %s\n", state->stale ? "no" : "yes");
	}

	fclose(io);
	close(fd);
}

static int update(db_t *db, state_t *state, char *name, uint32_t ts, uint8_t code, const char *msg)
{
	if (!state) {
		logger(LOG_ERR, "kernel unable to update NULL state");
		return -1;
	}
	if (!msg) {
		logger(LOG_ERR, "kernel unable to update state '%s': no summary message given", name);
		return -1;
	}

	logger(LOG_INFO, "updating state %s, status=%i, ts=%i, msg=[%s]", name, code, ts, msg);

	free(state->summary);
	state->status    = code;
	state->summary   = strdup(msg);
	state->last_seen = ts;
	state->expiry    = ts + state->type->freshness;
	state->stale     = 0;
	return 0;
}

static void* cleanup_kernel(void *_)
{
	kernel_t *db = (kernel_t*)_;
	if (db->listener) {
		logger(LOG_INFO, "kernel cleaning up; closing listening socket");
		vzmq_shutdown(db->listener, 500);
		db->listener = NULL;
	}
	if (db->broadcast) {
		logger(LOG_INFO, "kernel cleaning up; closing broadcast socket");
		vzmq_shutdown(db->broadcast, 0);
		db->broadcast = NULL;
	}
	if (db->server) {
		logger(LOG_INFO, "kernel cleaning up; saving final state to %s",
			db->server->config.savefile);
		if (save_state(&db->server->db, db->server->config.savefile) != 0) {
			logger(LOG_CRIT, "failed to save final state to %s: %s",
				db->server->config.savefile, strerror(errno));
		}
	}

	free(db);
	return NULL;
}

void* kernel(void *u)
{
	kernel_t *db = calloc(1, sizeof(kernel_t));
	pthread_cleanup_push(cleanup_kernel, db);

	db->server = (server_t*)u;
	if (!db->server) {
		logger(LOG_CRIT, "kernel failed: server context was NULL");
		return NULL;
	}

	db->listener = zmq_socket(db->server->zmq, ZMQ_ROUTER);
	if (!db->listener) {
		logger(LOG_CRIT, "kernel failed to get a ROUTER socket");
		return NULL;
	}
	if (zmq_bind(db->listener, KERNEL_ENDPOINT) != 0) {
		logger(LOG_CRIT, "kernel failed to bind to " KERNEL_ENDPOINT);
		return NULL;
	}

	if (!db->server->config.broadcast) {
		logger(LOG_CRIT, "kernel failed: no broadcast bind address specified");
		return NULL;
	}
	db->broadcast = zmq_socket(db->server->zmq, ZMQ_PUB);
	if (!db->broadcast) {
		logger(LOG_CRIT, "kernel failed to get a PUB socket");
		return NULL;
	}
	if (zmq_bind(db->broadcast, db->server->config.broadcast) != 0) {
		logger(LOG_CRIT, "kernel failed to bind to %s", db->server->config.broadcast);
		return NULL;
	}

	if (!db->server->config.savefile) {
		logger(LOG_CRIT, "kernel failed: no savefile provided");
		return NULL;
	}
	if (read_state(&db->server->db, db->server->config.savefile) != 0) {
		logger(LOG_WARNING, "kernel failed to read state from %s: %s",
				db->server->config.savefile, strerror(errno));
	}

	pdu_t *q, *a, *b = NULL;
	while ((q = pdu_recv(db->listener)) != NULL) {
		if (!pdu_type(q)) {
			logger(LOG_ERR, "kernel received an empty PDU; ignoring");
			continue;
		}
		if (strcmp(pdu_type(q), "PUT.STATE") == 0) {
			char *ts   = pdu_string(q, 1);
			char *name = pdu_string(q, 2);
			char *code = pdu_string(q, 3);
			char *msg  = pdu_string(q, 4);

			state_t *state = hash_get(&db->server->db.states, name);
			if (state) {
				if (update(&db->server->db, state, name, strtol(ts, NULL, 10), atoi(code), msg) == 0) {
					a = pdu_reply(q, "OK", 0);
					b = pdu_make("STATE", 1, name);
					pdu_extendf(b, "%li", state->last_seen);
					pdu_extendf(b, "%s",  state->stale ? "stale" : "fresh");
					pdu_extendf(b, "%s",  statstr(state->status));
					pdu_extendf(b, "%s",  state->summary);
				} else {
					a = pdu_reply(q, "ERROR", 1, "Update Failed");
				}
			} else {
				a = pdu_reply(q, "ERROR", 1, "State Not Found");
			}

			free(ts);
			free(name);
			free(code);
			free(msg);

		} else if (strcmp(pdu_type(q), "GET.STATE") == 0) {
			char *name = pdu_string(q, 1);
			state_t *state = hash_get(&db->server->db.states, name);
			if (!state) {
				a = pdu_reply(q, "ERROR", 1, "State Not Found");
			} else {
				a = pdu_reply(q, "STATE", 1, name);
				pdu_extendf(a, "%li", state->last_seen);
				pdu_extendf(a, "%s",  state->stale ? "stale" : "fresh");
				pdu_extendf(a, "%s",  statstr(state->status));
				pdu_extendf(a, "%s",  state->summary);
			}
			free(name);

		} else if (strcmp(pdu_type(q), "DUMP") == 0) {
			char *key  = pdu_string(q, 1);
			char *file = string(db->server->config.dumpfiles, key);
			free(key);

			dump(&db->server->db, file);
			a = pdu_reply(q, "DUMP", 1, file);
			free(file);

		} else if (strcmp(pdu_type(q), "CHECKFRESH") == 0) {
			check_freshness(&db->server->db);
			a = pdu_reply(q, "OK", 0);

		} else if (strcmp(pdu_type(q), "SAVESTATE") == 0) {
			if (save_state(&db->server->db, db->server->config.savefile) != 0) {
				a = pdu_reply(q, "ERROR", 1, "Internal Error");
			} else {
				a = pdu_reply(q, "OK", 0);
			}

		} else {
			a = pdu_reply(q, "ERROR", 1, "Invalid PDU");
		}

		pdu_send_and_free(a, db->listener);
		pdu_free(q);

		if (b) {
			pdu_send_and_free(b, db->broadcast);
			b = NULL;
		}
	}

	pthread_cleanup_pop(1);
	return NULL;
}