#include "test.h"

TESTS {
	subtest { /* single-file */
		db_t db;
		memset(&db, 0, sizeof(db));

		server_t s;
		memset(&s, 0, sizeof(s));

		write_file("t/tmp/config",
			"# test bolo configuration\n"
			"\n"
			"# network endpoints\n"
			"listener  tcp://*:4444\n"
			"control   tcp://*:5555\n"
			"nsca.port 5668\n"
			"\n"
			"# logging\n"
			"log critical daemon\n"
			"\n"
			"# file paths\n"
			"savefile  t/tmp/save\n"
			"dumpfiles t/tmp/dump.\%s\n"
			"\n"
			"type :local {\n"
			"  freshness 300\n"
			"  critical \"local check stale!\"\n"
			"}\n"
			"type :cloud {\n"
			"  freshness 900\n"
			"  warning \"no result in over 15min\"\n"
			"}\n"
			"\n"
			"use :local\n"
			"state host01/cpu\n"
			"state host02/cpu\n"
			"state :cloud site/online\n"
			"state host01/memory\n"
			"", 0);

		ok(configure("t/tmp/config", &s, &db) == 0,
			"Read configuration from t/tmp/config");

		is(s.config.bolo_endpoint, "tcp://*:4444",   "listener set");
		is(s.config.stat_endpoint, "tcp://*:5555",   "control set");
		is_int(s.config.nsca_port,  5668,            "nsca.port set");
		is(s.config.log_level,     "critical",       "log level set");
		is(s.config.log_facility,  "daemon",         "log facility set");
		is(s.config.savefile,      "t/tmp/save",     "savefile set");
		is(s.config.dumpfiles,     "t/tmp/dump.\%s", "dumpfiles set");

		type_t *t;
		t = hash_get(&db.types, ":local");
		isnt_null(t, "type :local created");
		if (t) {
		is(t->status, CRITICAL, ":local checks go CRITICAL when stale");
		is(t->summary, "local check stale!", ":local checks stale summary");
		}

		t = hash_get(&db.types, ":cloud");
		isnt_null(t, "type :cloud created");
		if (t) {
		is(t->status, WARNING, ":cloud checks go WARNING when stale");
		is(t->summary, "no result in over 15min", ":cloud checks stale summary");
		}
	}
}
