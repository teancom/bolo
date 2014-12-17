# Makefile.am

AUTOMAKE_OPTIONS = foreign subdir-objects
ACLOCAL_AMFLAGS = -I build

AM_CFLAGS =
AM_CFLAGS += -Wall
AM_CFLAGS += -g
AM_CFLAGS += @GCOV_CFLAGS@

CORE_SRC  =
CORE_SRC += src/bolo.h
CORE_SRC += src/nsca.c

sbin_PROGRAMS = bolo
bolo_SOURCES = $(CORE_SRC) src/daemon.c

CTAP_TESTS  =
CTAP_TESTS += t/00-nsca

TEST_SRC  = $(CORE_SRC)
TEST_SRC += t/test.h

TESTS = $(CTAP_TESTS)
check_PROGRAMS = $(CTAP_TESTS)
t_00_nsca_SOURCES = $(TEST_SRC) t/00-nsca.c
t_00_nsca_LDADD   = -lpthread -lzmq




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
