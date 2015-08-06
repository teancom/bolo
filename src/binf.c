#include "bolo.h"

#define RECORD_TYPE_MASK  0x000f
#define RECORD_TYPE_STATE    0x1
#define RECORD_TYPE_COUNTER  0x2
#define RECORD_TYPE_SAMPLE   0x3
#define RECORD_TYPE_EVENT    0x4
#define RECORD_TYPE_RATE     0x5

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

typedef struct PACKED {
	uint32_t first_seen;
	uint32_t last_seen;
	uint64_t first;
	uint64_t last;
} binf_rate_t;

typedef struct PACKED {
	uint32_t  timestamp;
} binf_event_t;

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

static int s_write_record(int fd, ssize_t *len, uint8_t type, void *_)
{
	binf_record_t record;
	union {
		binf_state_t   state;
		binf_counter_t counter;
		binf_sample_t  sample;
		binf_event_t   event;
		binf_rate_t    rate;
	} body;
	union {
		void      *unknown;
		state_t   *state;
		counter_t *counter;
		sample_t  *sample;
		event_t   *event;
		rate_t    *rate;
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

	case RECORD_TYPE_EVENT:
		record.len += sizeof(body.event);
		record.len += strlen(payload.event->name)  + 1;
		record.len += strlen(payload.event->extra) + 1;
		break;

	case RECORD_TYPE_RATE:
		record.len += sizeof(body.rate);
		record.len += strlen(payload.rate->name) + 1;
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
		body.sample.n         = htonll(payload.sample->n);
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

	case RECORD_TYPE_EVENT:
		body.event.timestamp = htonl(payload.event->timestamp);

		n = write(fd, &body.event, want = sizeof(body.event));
		so_far += n;
		if (n != want) logger(LOG_CRIT, "wanted %i / got %i\n", want, n);
		if (n != want)
			return so_far;

		s = payload.event->name;
		n = write(fd, s, want = strlen(s) + 1);
		so_far += n;
		if (n != want) logger(LOG_CRIT, "wanted %i / got %i\n", want, n);
		if (n != want)
			return so_far;

		s = payload.event->extra;
		n = write(fd, s, want = strlen(s) + 1);
		so_far += n;
		if (n != want) logger(LOG_CRIT, "wanted %i / got %i\n", want, n);
		if (n != want)
			return so_far;

		break;

	case RECORD_TYPE_RATE:
		body.rate.first_seen = htonl(payload.rate->first_seen);
		body.rate.last_seen  = htonl(payload.rate->last_seen);
		body.rate.first      = htonll(payload.rate->first);
		body.rate.last       = htonll(payload.rate->last);

		n = write(fd, &body.rate, want = sizeof(body.rate));
		so_far += n;
		if (n != want) logger(LOG_CRIT, "wanted %i / got %i\n", want, n);
		if (n != want)
			return so_far;

		s = payload.rate->name;
		n = write(fd, s, want = strlen(s) + 1);
		so_far += n;
		if (n != want) logger(LOG_CRIT, "wanted %i / got %i\n", want, n);
		if (n != want)
			return so_far;

		break;

	default:
		return -1;
	}

	return 0;
}

static int s_read_record(int fd, uint8_t *type, void **r)
{
	binf_record_t record;
	union {
		binf_state_t   state;
		binf_counter_t counter;
		binf_sample_t  sample;
		binf_event_t   event;
		binf_rate_t    rate;
	} body;
	union {
		void      *unknown;
		state_t   *state;
		counter_t *counter;
		sample_t  *sample;
		event_t   *event;
		rate_t    *rate;
	} payload;
	ssize_t n, want;
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
		if (!payload.state)
			return 1;

		n = read(fd, &body.state, want = sizeof(body.state));
		if (n != want) {
			free(payload.state);
			return 1;
		}

		payload.state->last_seen = ntohl(body.state.last_seen);
		payload.state->status    = body.state.status;
		payload.state->stale     = body.state.stale;

		want = record.len - sizeof(record) - sizeof(body.state);
		if (want > 4095) {
			free(payload.state);
			return 1;
		}
		n = read(fd, buf, want);
		if (n != want) {
			free(payload.state);
			return 1;
		}
		buf[n] = '\0';
		for (p = buf; *p++; );
		if (p - buf + 1 == want) {
			free(payload.state);
			return 1;
		}
		payload.state->name    = strdup(buf);
		payload.state->summary = strdup(p);

		*r = payload.state;
		return 0;

	case RECORD_TYPE_COUNTER:
		payload.counter = calloc(1, sizeof(counter_t));
		if (!payload.counter)
			return 1;

		n = read(fd, &body.counter, want = sizeof(body.counter));
		if (n != want) {
			free(payload.counter);
			return 1;
		}

		payload.counter->last_seen = ntohl(body.counter.last_seen);
		payload.counter->value     = ntohll(body.counter.value);

		want = record.len - sizeof(record) - sizeof(body.counter);
		if (want > 4095) {
			free(payload.counter);
			return 1;
		}
		n = read(fd, buf, want);
		if (n != want) {
			free(payload.counter);
			return 1;
		}
		buf[n] = '\0';
		payload.counter->name = strdup(buf);

		*r = payload.counter;
		return 0;

	case RECORD_TYPE_SAMPLE:
		payload.sample = calloc(1, sizeof(sample_t));

		n = read(fd, &body.sample, want = sizeof(body.sample));
		if (n != want) {
			free(payload.sample);
			return 1;
		}

		payload.sample->last_seen = ntohl(body.sample.last_seen);
		payload.sample->n         = ntohll(body.sample.n);
		payload.sample->min       = ntohl(body.sample.min);
		payload.sample->max       = ntohl(body.sample.max);
		payload.sample->sum       = ntohl(body.sample.sum);
		payload.sample->mean      = ntohl(body.sample.mean);
		payload.sample->mean_     = ntohl(body.sample.mean_);
		payload.sample->var       = ntohl(body.sample.var);
		payload.sample->var_      = ntohl(body.sample.var_);

		want = record.len - sizeof(record) - sizeof(body.sample);
		if (want > 4095) {
			free(payload.sample);
			return 1;
		}
		n = read(fd, buf, want);
		if (n != want) {
			free(payload.sample);
			return 1;
		}
		buf[n] = '\0';
		payload.sample->name = strdup(buf);

		*r = payload.sample;
		return 0;

	case RECORD_TYPE_EVENT:
		payload.event = calloc(1, sizeof(event_t));
		if (!payload.event)
			return 1;

		n = read(fd, &body.event, want = sizeof(body.event));
		if (n != want) {
			free(payload.event);
			return 1;
		}

		payload.event->timestamp = ntohl(body.event.timestamp);
		want = record.len - sizeof(record) - sizeof(body.event);
		if (want > 4095) {
			free(payload.event);
			return 1;
		}
		n = read(fd, buf, want);
		if (n != want) {
			free(payload.event);
			return 1;
		}
		buf[n] = '\0';
		for (p = buf; *p++; );
		if (p - buf + 1 == want) {
			free(payload.event);
			return 1;
		}
		payload.event->name  = strdup(buf);
		payload.event->extra = strdup(p);

		*r = payload.event;
		return 0;

	case RECORD_TYPE_RATE:
		payload.rate = calloc(1, sizeof(rate_t));
		if (!payload.rate)
			return 1;

		n = read(fd, &body.rate, want = sizeof(body.rate));
		if (n != want) {
			free(payload.rate);
			return 1;
		}

		payload.rate->first_seen = ntohl(body.rate.first_seen);
		payload.rate->last_seen  = ntohl(body.rate.last_seen);
		payload.rate->first      = ntohll(body.rate.first);
		payload.rate->last       = ntohll(body.rate.last);

		want = record.len - sizeof(record) - sizeof(body.rate);
		if (want > 4095) {
			free(payload.rate);
			return 1;
		}
		n = read(fd, buf, want);
		if (n != want) {
			free(payload.rate);
			return 1;
		}
		buf[n] = '\0';
		for (p = buf; *p++; );
		if (p - buf + 1 == want) {
			free(payload.rate);
			return 1;
		}
		payload.rate->name = strdup(buf);

		*r = payload.rate;
		return 0;

	default:
		return 1;
	}

	return 1;
}

int binf_write(db_t *db, const char *file)
{
	binf_header_t header;

	state_t   *state;
	counter_t *counter;
	sample_t  *sample;
	event_t   *event;
	rate_t   *rate;

	char *name;
	int i, n;
	ssize_t len;

	int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0640);
	if (fd < 0) {
		logger(LOG_ERR, "kernel failed to open save file %s for writing: %s",
				file, strerror(errno));
		return -1;
	}

	logger(LOG_NOTICE, "saving state db to %s", file);

	memset(&header, 0, sizeof(header));
	memcpy(&header.magic, "BOLO", 4);
	header.version   = htons(1);
	header.flags     = 0;
	header.timestamp = htonl((uint32_t)time_s());

	header.count = 0;
	for_each_key_value(&db->states,   name, state)   header.count++;
	for_each_key_value(&db->counters, name, counter) header.count++;
	for_each_key_value(&db->samples,  name, sample)  header.count++;
	for_each_object(event, &db->events, l)           header.count++;
	for_each_key_value(&db->rates,    name, rate)    header.count++;
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
		n = s_write_record(fd, &len, RECORD_TYPE_STATE, state);
		if (n != 0) {
			logger(LOG_ERR, "only wrote %i of %i bytes for state record #%i (%s); savefile %s is probably corrupt now",
				n, len, i, name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i bytes for counter record #%i (%s)", len, i, name);
		i++;
	}
	for_each_key_value(&db->counters, name, counter) {
		n = s_write_record(fd, &len, RECORD_TYPE_COUNTER, counter);
		if (n != 0) {
			logger(LOG_ERR, "only wrote %i of %i bytes for counter record #%i (%s); savefile %s is probably corrupt now",
				n, len, i, name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i bytes for counter record #%i (%s)", len, i, name);
		i++;
	}
	for_each_key_value(&db->samples, name, sample) {
		n = s_write_record(fd, &len, RECORD_TYPE_SAMPLE, sample);
		if (n != 0) {
			logger(LOG_ERR, "only wrote %i of %i bytes for samples record #%i (%s); savefile %s is probably corrupt now",
				n, len, i, name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i bytes for samples record #%i (%s)", len, i, name);
		i++;
	}
	event_t *ev;
	for_each_object(ev, &db->events, l) {
		n = s_write_record(fd, &len, RECORD_TYPE_EVENT, ev);
		if (n != 0) {
			logger(LOG_ERR, "only wrote %i of %i bytes for event record #%i (%s); savefile %s is probably corrupt now",
				n, len, i, ev->name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i bytes for event record #%i (%s)", len, i, ev->name);
		i++;
	}
	for_each_key_value(&db->rates, name, rate) {
		n = s_write_record(fd, &len, RECORD_TYPE_RATE, rate);
		if (n != 0) {
			logger(LOG_ERR, "only wrote %i of %i bytes for rate record #%i (%s); savefile %s is probably corrupt now",
				n, len, i, name, file);
			close(fd);
			return -1;
		}
		logger(LOG_INFO, "wrote %i bytes for rate record #%i (%s)", len, i, name);
		i++;
	}
	n = write(fd, "\0\0", 2);
	if (n != 2) {
		logger(LOG_ERR, "failed to write trailer bytes; savefile %s is probably corrupt now",
			file);
		close(fd);
		return -1;
	}
	logger(LOG_INFO, "done writing savefile %s", file);
	close(fd);
	return 0;
}

int binf_read(db_t *db, const char *file)
{
	binf_header_t header;
	union {
		void      *unknown;
		state_t   *state;
		counter_t *counter;
		sample_t  *sample;
		event_t   *event;
		rate_t    *rate;
	} payload, found;
	unsigned int i, n;
	uint8_t type;

	int fd = open(file, O_RDONLY);
	if (fd < 0) {
		logger(LOG_ERR, "kernel failed to open %s for reading: %s",
			file, strerror(errno));
		return -1;
	}

	logger(LOG_NOTICE, "reading state db from savefile %s", file);
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

		if (s_read_record(fd, &type, &payload.unknown) != 0) {
			logger(LOG_ERR, "%s: failed to read all of record #%i", file, i);
			close(fd);
			return -1;
		}

		switch (type) {
		case RECORD_TYPE_STATE:
			if ((found.state = find_state(db, payload.state->name)) != NULL) {
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
			if ((found.counter = find_counter(db, payload.counter->name)) != NULL) {
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
			if ((found.sample = find_sample(db, payload.sample->name)) != NULL) {
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

		case RECORD_TYPE_EVENT:
			list_push(&db->events, &payload.event->l);
			break;

		case RECORD_TYPE_RATE:
			if ((found.rate = find_rate(db, payload.rate->name)) != NULL) {
				found.rate->first_seen = payload.rate->first_seen;
				found.rate->last_seen  = payload.rate->last_seen;
				found.rate->first      = payload.rate->first;
				found.rate->last       = payload.rate->last;
			} else {
				logger(LOG_INFO, "rate %s not found in configuration, skipping", payload.rate->name);
			}
			free(payload.rate->name);
			free(payload.rate);
			payload.rate = NULL;
			break;

		default:
			logger(LOG_ERR, "unknown record type %02x found!", type);
			close(fd);
			return 1;
		}
	}
	char trailer[2] = { 1 };
	n = read(fd, trailer, 2);
	if (n != 2 || trailer[0] || trailer[1]) {
		logger(LOG_ERR, "no savefile trailer found!");
		close(fd);
		return 1;
	}

	logger(LOG_INFO, "done reading savefile %s", file);
	close(fd);
	return 0;
}
