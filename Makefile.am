# Makefile.am

AUTOMAKE_OPTIONS = foreign subdir-objects
ACLOCAL_AMFLAGS = -I build
EXTRA_DIST =

AM_CFLAGS =
AM_CFLAGS += -Wall
AM_CFLAGS += -g
AM_CFLAGS += @GCOV_CFLAGS@

LDADD = -lvigor -lpthread -lzmq -lpcre

CORE_SRC  =
CORE_SRC += src/bolo.h
CORE_SRC += src/kernel.c
CORE_SRC += src/scheduler.c
CORE_SRC += src/listener.c
CORE_SRC += src/nsca.c
CORE_SRC += src/controller.c
CORE_SRC += src/config.c
CORE_SRC += src/data.c
CORE_SRC += src/util.c
CORE_SRC += src/binf.c
CORE_SRC += src/send.c

EXTRA_DIST += README.md
EXTRA_DIST += PROTO
EXTRA_DIST += bootstrap
EXTRA_DIST += man examples
EXTRA_DIST += util/packet
EXTRA_DIST += util/savefile
EXTRA_DIST += util/sender

sbin_PROGRAMS = bolo bolo2log bolo2console dbolo
bolo_SOURCES            = $(CORE_SRC) src/daemon.c
bolo2log_SOURCES        = $(CORE_SRC) src/sub_log.c
bolo2console_SOURCES    = $(CORE_SRC) src/sub_console.c
dbolo_SOURCES           = $(CORE_SRC) src/agent.c

if build_rrd_subscriber
sbin_PROGRAMS          += bolo2rrd
bolo2rrd_SOURCES        = $(CORE_SRC) src/sub_rrd.c
endif

if build_pg_subscriber
sbin_PROGRAMS          += bolo2pg
bolo2pg_SOURCES         = $(CORE_SRC) src/sub_pg.c
bolo2pg_LDADD           = -lpq
endif

bin_PROGRAMS = send_bolo stat_bolo bolo_nsca bolospy
send_bolo_SOURCES = $(CORE_SRC) src/send_bolo.c
stat_bolo_SOURCES = $(CORE_SRC) src/stat_bolo.c
bolo_nsca_SOURCES = $(CORE_SRC) src/bolo_nsca.c
bolospy_SOURCES   = $(CORE_SRC) src/spy.c

CTAP_TESTS  =
CTAP_TESTS += t/00-config
CTAP_TESTS += t/01-kernel
CTAP_TESTS += t/02-sched
CTAP_TESTS += t/03-lsnr
CTAP_TESTS += t/04-ctrl
CTAP_TESTS += t/05-nsca
CTAP_TESTS += t/06-fresh
CTAP_TESTS += t/07-roll
CTAP_TESTS += t/08-tick
CTAP_TESTS += t/09-sample
CTAP_TESTS += t/10-match
CTAP_TESTS += t/11-events
CTAP_TESTS += t/12-rate

CTAP_TESTS += t/fail01-db
CTAP_TESTS += t/fail02-db
CTAP_TESTS += t/fail03-db
CTAP_TESTS += t/fail04-db
CTAP_TESTS += t/fail05-db
CTAP_TESTS += t/fail06-db

CTAP_TESTS += t/vx-pdu

SHELL_TESTS  =
SHELL_TESTS += t/memcheck/00-units.t

TEST_SRC  = $(CORE_SRC)
TEST_SRC += t/test.h

TEST_EXTRAS  =
TEST_EXTRAS += t/memcheck/verify
TEST_EXTRAS += $(SHELL_TESTS)
EXTRA_DIST  += $(TEST_EXTRAS)

TESTS = $(CTAP_TESTS) $(SHELL_TESTS)
check_PROGRAMS = $(CTAP_TESTS)
t_00_config_SOURCES = $(TEST_SRC) t/00-config.c
t_01_kernel_SOURCES = $(TEST_SRC) t/01-kernel.c
t_02_sched_SOURCES  = $(TEST_SRC) t/02-sched.c
t_03_lsnr_SOURCES   = $(TEST_SRC) t/03-lsnr.c
t_04_ctrl_SOURCES   = $(TEST_SRC) t/04-ctrl.c
t_05_nsca_SOURCES   = $(TEST_SRC) t/05-nsca.c
t_06_fresh_SOURCES  = $(TEST_SRC) t/06-fresh.c
t_07_roll_SOURCES   = $(TEST_SRC) t/07-roll.c
t_08_tick_SOURCES   = $(TEST_SRC) t/08-tick.c
t_09_sample_SOURCES = $(TEST_SRC) t/09-sample.c
t_10_match_SOURCES  = $(TEST_SRC) t/10-match.c
t_11_events_SOURCES = $(TEST_SRC) t/11-events.c
t_12_rate_SOURCES   = $(TEST_SRC) t/12-rate.c

t_fail01_db_SOURCES = $(TEST_SRC) t/fail01-db.c
t_fail02_db_SOURCES = $(TEST_SRC) t/fail02-db.c
t_fail03_db_SOURCES = $(TEST_SRC) t/fail03-db.c
t_fail04_db_SOURCES = $(TEST_SRC) t/fail04-db.c
t_fail05_db_SOURCES = $(TEST_SRC) t/fail05-db.c
t_fail06_db_SOURCES = $(TEST_SRC) t/fail06-db.c

t_vx_pdu_SOURCES    = $(TEST_SRC) t/vx-pdu.c

dist_man_MANS  =
dist_man_MANS += man/bolo.8
dist_man_MANS += man/bolo2pg.8
dist_man_MANS += man/bolo2rrd.8
dist_man_MANS += man/bolo.conf.5
dist_man_MANS += man/send_bolo.1
dist_man_MANS += man/stat_bolo.1

man/%.1: man/%.1.pod
	./man/build $< @PACKAGE_VERSION@ > $@
man/%.5: man/%.5.pod
	./man/build $< @PACKAGE_VERSION@ > $@
man/%.7: man/%.7.pod
	./man/build $< @PACKAGE_VERSION@ > $@
man/%.8: man/%.8.pod
	./man/build $< @PACKAGE_VERSION@ > $@

.PHONY: coverage-clean coverage-report coverage clean-local-cov distclean-local
distclean-local:
	rm -rf t/tmp

clean-local: clean-local-gcda
clean-local-gcda:
	touch sac.gcda
	find . -name '*.gc??' | xargs rm

if GCOV_ENABLED
coverage-clean:
	@rm -fr coverage
	@find . -name "*.gcda" -exec rm {} \;
	@lcov --directory . --zerocounters

coverage-report:
	@mkdir -p coverage
	@lcov --compat-libtool --directory . --base-directory . --capture --output-file coverage/app.info
	@genhtml -o coverage/ coverage/app.info

coverage:
	@make coverage-report

clean-local-cov:
	@make coverage-clean

check:
	@make coverage

else
coverage:
	@echo >&2 "nothin doin"
clean-local-cov:
endif

# aliases
test: check

############################################################

version:
	@echo $(VERSION)

distfile: dist
	@echo $(PACKAGE)-$(VERSION).tar.gz

manifest:
	@echo >&2 "nothin doin"

test: check
