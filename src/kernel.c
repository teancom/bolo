#include "bolo.h"
#include <arpa/inet.h>

typedef struct PACKED {
	uint32_t  magic;
	uint16_t  version;
	uint16_t  flags;
	uint32_t  timestamp;
	uint32_t  count;
} binf_header_t;

typedef struct PACKED {
	uint16_t  len;
	uint32_t  last_seen;
	 uint8_t  status;
	 uint8_t  stale;
} binf_record_t;

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

static int save_state(db_t *db, const char *file)
{
	binf_header_t header;
	binf_record_t record;
	char *name; state_t *state;
	int i;
	size_t n;

	int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0640);
	if (fd < 0) {
		logger(LOG_ERR, "kernel failed to open save file %s for writing: %s",
				file, strerror(errno));
		return -1;
	}

	logger(LOG_NOTICE, "saving db state to %s", file);

	memcpy(&header.magic, "BOLO", 4);
	header.version   = htons(1);
	header.flags     = 0;
	header.timestamp = htonl((uint32_t)time_s());

	/* FIXME: need a hash_len(&h) in libvigor */
	header.count = 0;
	for_each_key_value(&db->states, name, state)
		header.count++;
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
		size_t l_name    = strlen(name)           + 1;
		size_t l_summary = strlen(state->summary) + 1;
		record.len       = htons(sizeof(record) + l_name + l_summary);
		record.last_seen = htonl(state->last_seen);
		record.status    = state->status;
		record.stale     = state->stale;

		size_t so_far    = 0;
		n = write(fd, &record, sizeof(record));
		if (n != sizeof(record)) {
			logger(LOG_ERR, "only wrote %i of %i bytes for record #%i (%s); savefile %s is probably corrupt now",
				so_far + n, record.len, i, name, file);
			close(fd);
			return -1;
		}
		so_far += n;
		n = write(fd, name, l_name);
		if (n != l_name) {
			logger(LOG_ERR, "only wrote %i of %i bytes for record #%i (%s); savefile %s is probably corrupt now",
				so_far + n, record.len, i, name, file);
			close(fd);
			return -1;
		}
		so_far += n;
		n = write(fd, state->summary, l_summary);
		if (n != l_summary) {
			logger(LOG_ERR, "only wrote %i of %i bytes for record #%i (%s); savefile %s is probably corrupt now",
				so_far + n, record.len, i, name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i of %i bytes for record #%i (%s)", so_far + n, i, name, name);
		i++;
	}
	logger(LOG_INFO, "done writing savefile %s", file);
	close(fd);
	return 0;
}

static int read_state(db_t *db, const char *file)
{
	binf_header_t header;
	binf_record_t record;
	ssize_t n;
	int i;

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
		logger(LOG_INFO, "reading state record #%i from savefile", i);

		n = read(fd, &record, sizeof(record));
		if (n != sizeof(record)) {
			logger(LOG_ERR, "%s: only read %i bytes of record #%i, expected to read %i",
				file, n, sizeof(record));
			close(fd);
			return -1;
		}

		record.len       = ntohs(record.len);
		record.last_seen = ntohl(record.last_seen);
		if (record.len == 0 || record.len < sizeof(binf_record_t)) {
			logger(LOG_ERR, "%s: record #%i reports an invalid length of %u; is the savefile corrupt?",
				file, i, record.len);
			close(fd);
			return -1;
		}

		size_t rest = record.len - sizeof(binf_record_t);
		char *strings = calloc(rest, sizeof(char));
		n = read(fd, strings, rest);
		if (n != rest) {
			logger(LOG_ERR, "%s: only read %i of %i bytes for record #%i payload, is the savefile corrupt?",
				file, n, rest, i);
			free(strings);
			close(fd);
			return -1;
		}

		logger(LOG_INFO, "read record #%i (%s)", i, strings);
		state_t *s = hash_get(&db->states, strings);
		if (s) {
			free(s->summary);
			s->summary   = strdup(strings + strlen(strings) + 1);
			s->last_seen = record.last_seen;
			s->status    = record.status;
			s->stale     = record.stale;
		} else {
			logger(LOG_INFO, "state %s not found in configuration, skipping", strings);
		}
		free(strings);
	}
	logger(LOG_INFO, "done writing savefile %s", file);
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
