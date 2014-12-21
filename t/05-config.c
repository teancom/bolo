#include "test.h"

TESTS {
	alarm(5);
	subtest { /* single-file */
		server_t svr; memset(&svr, 0, sizeof(svr));

		write_file("t/tmp/config",
			"# test configuration\n"
			"\n"
			"# network endpoints\n"
			"listener   tcp://*:4242\n"
			"controller tcp://*:5555\n"
			"broadcast  tcp://*:6868\n"
			"\n"
			"user       monitor\n"
			"group      amgs\n"
			"pidfile    /var/run/bolo.pid\n"
			"\n"
			"# logging\n"
			"log critical daemon\n"
			"\n"
			"# file paths\n"
			"savefile  t/tmp/save\n"
			"dumpfiles /tmp/dump.\%s\n"
			"\n"
			"type :test {\n"
			"  freshness 300\n"
			"  critical \"local check stale!\"\n"
			"}\n"
			"type :cloud {\n"
			"  freshness 900\n"
			"  warning \"no result in over 15min\"\n"
			"}\n"
			"\n"
			"type :unused {\n"
			"  freshness 5\n"
			"  unknown whatever\n"
			"}\n"
			"\n"
			"use :test\n"
			"state host01/cpu\n"
			"state host02/cpu\n"
			"state :cloud site/online\n"
			"state host01/memory\n"
			"", 0);

		int32_t now = time_s();
		ok(configure("t/tmp/config", &svr) == 0,
			"Read configuration from t/tmp/config");

		is(svr.config.listener,      "tcp://*:4242",   "listener set");
		is(svr.config.controller,    "tcp://*:5555",   "controller set");
		is(svr.config.broadcast,     "tcp://*:6868",   "broadcast set");
		is(svr.config.log_level,     "critical",       "log level set");
		is(svr.config.log_facility,  "daemon",         "log facility set");
		is(svr.config.runas_user,    "monitor",        "run-as user set");
		is(svr.config.runas_group,   "amgs",           "run-as group set");
		is(svr.config.savefile,      "t/tmp/save",     "savefile set");
		is(svr.config.savefile,      "t/tmp/save",     "savefile set");
		is(svr.config.dumpfiles,     "/tmp/dump.\%s",  "dumpfiles set");

		type_t *t;
		t = hash_get(&svr.db.types, ":test");
		isnt_null(t, "type :test created");
		is_int(t->status, CRITICAL, ":test checks go CRITICAL when stale");
		is(t->summary, "local check stale!", ":test checks stale summary");

		t = hash_get(&svr.db.types, ":cloud");
		isnt_null(t, "type :cloud created");
		is_int(t->status, WARNING, ":cloud checks go WARNING when stale");
		is(t->summary, "no result in over 15min", ":cloud checks stale summary");

		t = hash_get(&svr.db.types, ":enoent");
		is_null(t, "type :enoent not created (not in the file)");

		state_t *s;
		s = hash_get(&svr.db.states, "host01/cpu");
		isnt_null(s, "state host01/cpu created");
		is_pointer(s->type,
			hash_get(&svr.db.types, ":test"),  "host01/cpu is a :local check");
		is_int(s->last_seen, 0,             "host01/cpu has not yet been seen");
		ok(abs(now + 300 - s->expiry) < 5,  "host01/cpu expires within the next 5min");
		is_int(s->status, PENDING,          "host01/cpu status is PENDING");
		ok(!s->stale,                       "host01/cpu is not (yet) stale");

		s = hash_get(&svr.db.states, "site/online");
		isnt_null(s, "state site/online created");
		is_pointer(s->type,
			hash_get(&svr.db.types, ":cloud"),  "site/online is a :cloud check");
		is_int(s->last_seen, 0,             "site/online has not yet been seen");
		ok(abs(now + 900 - s->expiry) < 5,  "site/online expires within the next 15min");
		is_int(s->status, PENDING,          "site/online status is PENDING");
		ok(!s->stale,                       "site/online is not (yet) stale");
	}

	subtest { /* various configuration errors */
		server_t svr; memset(&svr, 0, sizeof(svr));

		write_file("t/tmp/config.bad", "listener :type\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with bad listener directive");

		write_file("t/tmp/config.bad", "whatever works\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with unknown directive");

		write_file("t/tmp/config.bad", "state\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with bad state directive");

		write_file("t/tmp/config.bad", "literal\1\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with non-alpha directive");

		write_file("t/tmp/config.bad", "use :::lots:of:colons\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with malformed type name");

		write_file("t/tmp/config.bad", "type :x { foobar }\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with bad type definition");

		write_file("t/tmp/config.bad", "type :x { { { {\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with REALLY bad type definition");

		write_file("t/tmp/config.bad", "state :enoent x\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with missing type");

		write_file("t/tmp/config.bad", "use :enoent\nstate x\n", 0);
		ok(configure("t/tmp/config.bad", &svr) != 0,
			"should fail to read configuration file with missing (default) type");
	}

	subtest { /* recoverable configuration errors */
		server_t svr; memset(&svr, 0, sizeof(svr));

		write_file("t/tmp/config.bad",
			"type :string {\n"
			"  freshness 60\n"
			"  critical \"this is an unterminated string literal\n"
			"}\n"
			"type :default { }\n"
			"type :automsg { freshness 60 }\n"
			"type :qhour   { freshness 900 }\n"
			"type :hourly  { freshness 3600 }\n"
			"type :hourly5 { freshness 3900 }\n"
			"type :short   { freshness 42 }\n"
			"", 0);
		ok(configure("t/tmp/config.bad", &svr) == 0,
			"can recover from unterminated string");

		type_t *t;
		t = hash_get(&svr.db.types, ":string");
		isnt_null(t, "type :string created");
		is_int(t->freshness, 60, ":string has a freshness threshold");
		is_int(t->status, CRITICAL, ":string type has a staleness status");
		is(t->summary, "this is an unterminated string literal", ":string type has a staleness summary");

		t = hash_get(&svr.db.types, ":default");
		isnt_null(t, "type :default created");
		is_int(t->freshness, 300, ":default has a default freshness threshold");
		is_int(t->status, WARNING, ":default type has a default staleness status of WARNING");
		is(t->summary, "No results received for more than 5 minutes", ":default type has a default staleness summary");

		t = hash_get(&svr.db.types, ":automsg");
		isnt_null(t, "type :automsg created");
		is_int(t->status, WARNING, ":automsg type has a default staleness status of WARNING");
		is(t->summary, "No results received for more than 1 minute", ":automsg type has a default staleness summary");

		t = hash_get(&svr.db.types, ":qhour");
		isnt_null(t, "type :qhour created");
		is(t->summary, "No results received for more than 15 minutes", ":qhour type has a default staleness summary");

		t = hash_get(&svr.db.types, ":hourly");
		isnt_null(t, "type :hourly created");
		is(t->summary, "No results received for more than 1 hour", ":hourly type has a default staleness summary");

		t = hash_get(&svr.db.types, ":hourly5");
		isnt_null(t, "type :hourly5 created");
		is(t->summary, "No results received for more than 1 hour", ":hourly5 type has a default staleness summary");

		t = hash_get(&svr.db.types, ":short");
		isnt_null(t, "type :short created");
		is(t->summary, "No results received for more than 42 seconds", ":short type has a default staleness summary");
	}
}
