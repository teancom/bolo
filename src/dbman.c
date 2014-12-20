#include "bolo.h"
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
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
		logger(LOG_ERR, "db manager failed to open save file %s for writing: %s",
				file, strerror(errno));
		return -1;
	}

	memcpy(&header.magic, "BOLO", 4);
	header.version   = htons(1);
	header.flags     = 0;
	header.timestamp = htonl((uint32_t)time_s());

	/* FIXME: need a hash_len(&h) in libvigor */
	header.count = 0;
	for_each_key_value(&db->states, name, state)
		header.count++;
	header.count = htonl(header.count);

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
		record.len       = htons(sizeof(binf_record_t) + l_name + l_summary);
		record.last_seen = htonl(state->last_seen);
		record.status    = state->status;
		record.stale     = state->stale;

		n = write(fd, &record, sizeof(record));
		if (n != sizeof(record)) {
			logger(LOG_ERR, "only wrote %i of %i bytes for record #%i (%s); savefile %s is probably corrupt now",
				n, sizeof(record), i, name, file);
			close(fd);
			return -1;
		}
		n = write(fd, name, l_name);
		if (n != l_name) {
			logger(LOG_ERR, "only wrote %i of %i bytes for record #%i (%s); savefile %s is probably corrupt now",
				n + sizeof(record), sizeof(record) + l_name, i, name, file);
			close(fd);
			return -1;
		}
		n = write(fd, state->summary, l_summary);
		if (n != l_summary) {
			logger(LOG_ERR, "only wrote %i of %i bytes for record #%i (%s); savefile %s is probably corrupt now",
				n + sizeof(record) + l_name, sizeof(record) + l_name + l_summary, i, name, file);
			close(fd);
			return -1;
		}

		i++;
	}
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
		logger(LOG_ERR, "db manager failed to open %s for reading: %s",
			file, strerror(errno));
		return -1;
	}

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

	if (header.version != 1) {
		logger(LOG_ERR, "%s is a v%u savefile; this version of bolo only supports v1 files",
			file, header.version);
		close(fd);
		return -1;
	}

	for (i = 1; i <= header.count; i++) {
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

		state_t *s = hash_get(&db->states, strings);
		if (s) {
			s->summary   = strdup(strings + strlen(strings) + 1);
			s->last_seen = record.last_seen;
			s->status    = record.status;
			s->stale     = record.stale;
		}
		free(strings);
	}

	close(fd);
	return 0;
}

static void check_freshness(db_t *db)
{
	state_t *state;
	char *k;
	uint32_t now = time_s();

	for_each_key_value(&db->states, k, state) {
		if (state->expiry > now) continue;

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
		logger(LOG_ERR, "db manager cannot dump state; no dump file name provided");
		return;
	}
	int fd = open(file, O_WRONLY|O_CREAT|O_EXCL, 0644);
	if (fd < 0) {
		logger(LOG_ERR, "db manager cannot dump state; unable to open %s for writing: %s",
				file, strerror(errno));
		return;
	}

	FILE *io = fdopen(fd, "w");
	if (!io) {
		logger(LOG_ERR, "db manager cannot dump state; failed to open %s for writing: %s",
				file, strerror(errno));
		return;
	}
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

static int update(db_t *db, uint32_t ts, const char *name, uint8_t code, const char *msg)
{
	if (!name) {
		logger(LOG_ERR, "db manager unable to update state: no name given");
		return 0;
	}
	if (!msg) {
		logger(LOG_ERR, "db manager unable to update state '%s': no summary message given", name);
		return 0;
	}

	state_t *state = hash_get(&db->states, name);
	if (!state) return -1;

	free(state->summary);
	state->status    = code;
	state->summary   = strdup(msg);
	state->last_seen = ts;
	state->expiry    = ts + state->type->freshness;
	state->stale     = 0;
	return 0;
}

void* db_manager(void *u)
{
	server_t *s = (server_t*)u;
	if (!s) {
		logger(LOG_CRIT, "db manager failed: server context was NULL");
		return NULL;
	}

	void *z = zmq_socket(s->zmq, ZMQ_ROUTER);
	if (!z) {
		logger(LOG_CRIT, "db manager failed to get a ROUTER socket");
		return NULL;
	}
	if (zmq_bind(z, DB_MANAGER_ENDPOINT) != 0) {
		logger(LOG_CRIT, "db manager failed to bind to " DB_MANAGER_ENDPOINT);
		return NULL;
	}

	if (read_state(&s->db, s->config.savefile) != 0) {
		logger(LOG_WARNING, "db manager failed to read state from %s: %s",
				s->config.savefile, strerror(errno));
	}

	pdu_t *q, *a;
	while ((q = pdu_recv(z)) != NULL) {
		if (!pdu_type(q)) {
			logger(LOG_ERR, "db manager received an empty PDU; ignoring");
			continue;
		}
		if (strcmp(pdu_type(q), "UPDATE") == 0) {
			char *ts   = pdu_string(q, 1);
			char *name = pdu_string(q, 2);
			char *code = pdu_string(q, 3);
			char *msg  = pdu_string(q, 4);

			if (update(&s->db, strtol(ts, NULL, 10), name, atoi(code), msg) == 0)
				a = pdu_reply(q, "OK", 0);
			else
				a = pdu_reply(q, "ERROR", 1, "State Not Found");

			free(ts);
			free(name);
			free(code);
			free(msg);

		} else if (strcmp(pdu_type(q), "STATE") == 0) {
			char *name = pdu_string(q, 1);
			state_t *state = hash_get(&s->db.states, name);
			if (!state) {
				a = pdu_reply(q, "ERROR", 1, "State Not Found");
			} else {
				a = pdu_reply(q, "STATE", 1, name);
				pdu_extendf(a, "%li", state->last_seen);
				pdu_extendf(a, "%s",  state->stale ? "no" : "yes");
				pdu_extendf(a, "%s",  statstr(state->status));
				pdu_extendf(a, "%s",  state->summary);
			}
			free(name);

		} else if (strcmp(pdu_type(q), "DUMP") == 0) {
			char *key  = pdu_string(q, 1);
			char *file = string(s->config.dumpfiles, key);
			free(key);

			dump(&s->db, file);
			a = pdu_reply(q, "DUMP", 1, file);
			free(file);

		} else if (strcmp(pdu_type(q), "CHECKFRESH") == 0) {
			check_freshness(&s->db);
			a = pdu_reply(q, "OK", 0);

		} else if (strcmp(pdu_type(q), "SAVESTATE") == 0) {
			if (save_state(&s->db, s->config.savefile) != 0) {
				a = pdu_reply(q, "ERROR", 1, "Internal Error");
			} else {
				a = pdu_reply(q, "OK", 0);
			}

		} else {
			a = pdu_reply(q, "ERROR", 1, "Invalid PDU");
		}

		pdu_send_and_free(a, z);
		pdu_free(q);
	}

	return NULL; /* LCOV_EXCL_LINE */
}
