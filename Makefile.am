# Makefile.am

AUTOMAKE_OPTIONS = foreign subdir-objects
ACLOCAL_AMFLAGS = -I build

AM_CFLAGS =
AM_CFLAGS += -Wall
AM_CFLAGS += -g
AM_CFLAGS += @GCOV_CFLAGS@

LDADD = -lpthread -lzmq

CORE_SRC  =
CORE_SRC += src/bolo.h
CORE_SRC += src/dbman.c
CORE_SRC += src/scheduler.c
CORE_SRC += src/listener.c
CORE_SRC += src/controller.c
CORE_SRC += src/config.c

sbin_PROGRAMS = bolo
bolo_SOURCES = $(CORE_SRC) src/daemon.c

CTAP_TESTS  =
CTAP_TESTS += t/01-dbman
CTAP_TESTS += t/02-sched
CTAP_TESTS += t/03-lsnr
CTAP_TESTS += t/04-ctrl
CTAP_TESTS += t/05-config
CTAP_TESTS += t/06-fresh

CTAP_TESTS += t/fail01-db
CTAP_TESTS += t/fail02-db
CTAP_TESTS += t/fail03-db
CTAP_TESTS += t/fail04-db
CTAP_TESTS += t/fail05-db
CTAP_TESTS += t/fail06-db

TEST_SRC  = $(CORE_SRC)
TEST_SRC += t/test.h

TESTS = $(CTAP_TESTS)
check_PROGRAMS = $(CTAP_TESTS)
t_01_dbman_SOURCES  = $(TEST_SRC) t/01-dbman.c
t_02_sched_SOURCES  = $(TEST_SRC) t/02-sched.c
t_03_lsnr_SOURCES   = $(TEST_SRC) t/03-lsnr.c
t_04_ctrl_SOURCES   = $(TEST_SRC) t/04-ctrl.c
t_05_config_SOURCES = $(TEST_SRC) t/05-config.c
t_06_fresh_SOURCES  = $(TEST_SRC) t/06-fresh.c

t_fail01_db_SOURCES = $(TEST_SRC) t/fail01-db.c
t_fail02_db_SOURCES = $(TEST_SRC) t/fail02-db.c
t_fail03_db_SOURCES = $(TEST_SRC) t/fail03-db.c
t_fail04_db_SOURCES = $(TEST_SRC) t/fail04-db.c
t_fail05_db_SOURCES = $(TEST_SRC) t/fail05-db.c
t_fail06_db_SOURCES = $(TEST_SRC) t/fail06-db.c

.PHONY: coverage-clean coverage-report coverage clean-local-cov
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
