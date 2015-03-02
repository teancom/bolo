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

#include "test.h"

TESTS {
	mkdir("t/tmp", 0755);

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
			"nsca.port  5669\n"
			"\n"
			"max.events 8h\n"
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
			"\n"
			"window @minutely 60\n"
			"window @hourly   3600\n"
			"\n"
			"counter @minutely counter1\n"
			"counter @minutely counter2\n"
			"sample  @minutely res.cpu.usage\n"
			"sample  @hourly   res.df:/\n"
			"sample  @hourly   res.df:/var\n"
			"\n"
			"use @hourly\n"
			"counter counter3\n"
			"sample  something.hourly\n"
			"\n"
			"counter 123 explicit-window\n"
			"sample  456 explicit-window\n"
			"\n"
			"# pattern matching\n"
			"counter @minutely m/tick./\n"
			"sample  300       m/web.*/\n"
			"state   :cloud    m/cloud\\d+\\./\n"
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

		is_int(svr.config.nsca_port, 5669, "nsca.port set");
		is_int(svr.config.events_max, 8 * 3600, "max.events set to 8h (in seconds)");
		is_int(svr.config.events_keep, EVENTS_KEEP_TIME, "max.events is a timeframe");

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

		ok(!list_isempty(&svr.db.state_matches), "have pattern matched states");
		re_state_t *re_s = list_head(&svr.db.state_matches, re_state_t, l);
		is_pointer(re_s->type,
			hash_get(&svr.db.types, ":cloud"), "pattern match is a :cloud check");


		window_t *w;
		w = hash_get(&svr.db.windows, "@minutely");
		isnt_null(w, "window @minutely created");
		is_int(w->time, 60, "window @minutely is on a 60s modulus");

		w = hash_get(&svr.db.windows, "@hourly");
		isnt_null(w, "window @houryl created");
		is_int(w->time, 3600, "window @hourly is on a 3600s modulus");

		w = hash_get(&svr.db.windows, "@enoent");
		is_null(w, "window @enoent not created");


		counter_t *c;
		c = hash_get(&svr.db.counters, "counter1");
		isnt_null(c, "counter counter1 created");
		is_pointer(c->window,
			hash_get(&svr.db.windows, "@minutely"), "counter1 is a @minutely counter");
		is_int(c->value, 0, "counter1 is initially 0");

		c = hash_get(&svr.db.counters, "counter2");
		isnt_null(c, "counter counter2 created");
		is_pointer(c->window,
			hash_get(&svr.db.windows, "@minutely"), "counter2 is a @minutely counter");

		c = hash_get(&svr.db.counters, "counter3");
		isnt_null(c, "counter counter3 created");
		is_pointer(c->window,
			hash_get(&svr.db.windows, "@hourly"), "counter3 is a @minutely counter");

		c = hash_get(&svr.db.counters, "enoent");
		is_null(c, "counter enoent not created");

		c = hash_get(&svr.db.counters, "explicit-window");
		isnt_null(c, "counter explicit-window created");
		is_int(c->window->time, 123, "counter explicit-window got an anon window");

		ok(!list_isempty(&svr.db.counter_matches), "have pattern matched counters");
		re_counter_t *re_c = list_head(&svr.db.counter_matches, re_counter_t, l);
		is_pointer(re_c->window,
			hash_get(&svr.db.windows, "@minutely"), "pattern match is @minutely");


		sample_t *sa;
		sa = hash_get(&svr.db.samples, "res.cpu.usage");
		isnt_null(sa, "sample res.cpu.usage created");
		is_int(sa->n, 0, "new sample res.cpu.usage has no members");
		is_pointer(sa->window,
			hash_get(&svr.db.windows, "@minutely"), "res.cpu.usage is a @minutely sample");

		sa = hash_get(&svr.db.samples, "res.df:/");
		isnt_null(sa, "sample res.df:/ created");
		is_pointer(sa->window,
			hash_get(&svr.db.windows, "@hourly"), "res.df:/ is an @hourly sample");

		sa = hash_get(&svr.db.samples, "res.df:/var");
		isnt_null(sa, "sample res.df:/var created");
		is_pointer(sa->window,
			hash_get(&svr.db.windows, "@hourly"), "res.df:/var is an @hourly sample");

		sa = hash_get(&svr.db.samples, "something.hourly");
		isnt_null(sa, "sample something.hourly created");
		is_pointer(sa->window,
			hash_get(&svr.db.windows, "@hourly"), "something.hourly is an @hourly sample");

		sa = hash_get(&svr.db.samples, "explicit-window");
		isnt_null(sa, "sample explicit-window created");
		is_int(sa->window->time, 456, "sample explicit-window got an anon window");

		ok(!list_isempty(&svr.db.sample_matches), "have pattern matched samples");
		re_sample_t *re_sa = list_head(&svr.db.sample_matches, re_sample_t, l);
		is_int(re_sa->window->time, 300, "pattern match is a 5m sample set");
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
