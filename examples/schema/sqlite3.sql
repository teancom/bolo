CREATE TABLE states (
	id      INTEGER PRIMARY KEY,
	name    TEXT NOT NULL UNIQUE,
	status  TEXT NOT NULL,
	message TEXT,

	first_seen INTEGER NOT NULL,
	last_seen  INTEGER NOT NULL,

	CHECK (last_seen >= first_seen)
);

CREATE TABLE history (
	id      INTEGER PRIMARY KEY,
	name    TEXT NOT NULL,
	status  TEXT NOT NULL,
	message TEXT,

	started_at           INTEGER NOT NULL,
	tentative_ended_at   INTEGER NOT NULL,
	ended_at             INTEGER DEFAULT NULL,

	UNIQUE (name, started_at),

	CHECK (tentative_ended_at >= started_at),
	CHECK (ended_at IS NULL OR ended_at >= started_at)
);

CREATE TABLE datapoints (
	id         INTEGER PRIMARY KEY,
	name       TEXT NOT NULL,
	type       TEXT NOT NULL,
	first_seen INTEGER NOT NULL,
	last_seen  INTEGER NOT NULL,

	UNIQUE (name, type),
	CHECK (last_seen >= first_seen)
);
