# Makefile.am

AUTOMAKE_OPTIONS = foreign subdir-objects
ACLOCAL_AMFLAGS = -I build
EXTRA_DIST =

AM_CFLAGS =
AM_CFLAGS += -Wall
AM_CFLAGS += -g
AM_CFLAGS += @GCOV_CFLAGS@

AM_CPPFLAGS = -I$(top_srcdir)/include

LOG_DRIVER = $(top_srcdir)/t/run

LDADD = -lvigor -lpthread -lzmq -lpcre

CORE_SRC  =
CORE_SRC += include/bolo.h
CORE_SRC += src/bolo.h
#CORE_SRC += src/nsca.c
CORE_SRC += src/config.c
CORE_SRC += src/data.c
CORE_SRC += src/util.c
CORE_SRC += src/binf.c
CORE_SRC += src/send.c
CORE_SRC += src/qname.c

SUBS_SRC  = $(CORE_SRC)
SUBS_SRC += include/bolo/subscriber.h
SUBS_SRC += src/subs.c

EXTRA_DIST += README.md
EXTRA_DIST += PROTO
EXTRA_DIST += bootstrap
EXTRA_DIST += man examples
EXTRA_DIST += redhat

sbin_PROGRAMS = bolo2log bolo2console bolo2meta dbolo
bolo2log_SOURCES        = $(SUBS_SRC) src/bolo2log/main.c
bolo2console_SOURCES    = $(SUBS_SRC) src/bolo2console/main.c
bolo2meta_SOURCES       = $(SUBS_SRC) src/bolo2meta/main.c
dbolo_SOURCES           = $(CORE_SRC) src/dbolo/main.c

if build_rrd_subscriber
sbin_PROGRAMS          += bolo2rrd
bolo2rrd_SOURCES        = $(SUBS_SRC) src/bolo2rrd/main.c
bolo2rrd_LDADD          = -lrrd_th
endif

if build_pg_subscriber
sbin_PROGRAMS          += bolo2pg
bolo2pg_SOURCES         = $(SUBS_SRC) src/bolo2pg/main.c
bolo2pg_LDADD           = -lpq
endif

if build_sqlite_subscriber
sbin_PROGRAMS         += bolo2sqlite
bolo2sqlite_SOURCES    = $(SUBS_SRC) src/bolo2sqlite/main.c
bolo2sqlite_LDADD      = -lsqlite3
endif

if build_redis_subscriber
sbin_PROGRAMS         += bolo2redis
bolo2redis_SOURCES     = $(SUBS_SRC) src/bolo2redis/main.c
bolo2redis_LDADD       = -lhiredis
endif

bin_PROGRAMS = bolo_nsca
bolo_nsca_SOURCES = $(CORE_SRC) src/bolo_nsca/main.c

bin_PROGRAMS += bolo
bolo_SOURCES  = $(SUBS_SRC)
bolo_SOURCES += src/bolo/main.c
bolo_SOURCES += src/bolo/cmd_aggregator.c   src/core.c
bolo_SOURCES += src/bolo/cmd_cache.c
bolo_SOURCES += src/bolo/cmd_forget.c
bolo_SOURCES += src/bolo/cmd_help.c
bolo_SOURCES += src/bolo/cmd_query.c
bolo_SOURCES += src/bolo/cmd_send.c
bolo_SOURCES += src/bolo/cmd_version.c

bin_PROGRAMS += bolospy qname
bolospy_SOURCES   = $(CORE_SRC) src/bolospy/main.c
qname_SOURCES     = $(CORE_SRC) bin/qname.c

check_SCRIPTS = t/usage t/config t/core t/broadcast t/large-payloads t/events \
                t/beacon t/bolo-send t/bolo-cache t/qname \
                t/rrd-subscriber t/meta-subscriber t/log-subscriber \
                t/forget
TESTS = $(check_SCRIPTS)
dist_check_SCRIPTS=$(check_SCRIPTS) t/run t/lib

dist_man_MANS  =
if build_pg_subscriber
dist_man_MANS += man/bolo2pg.8
endif
dist_man_MANS += man/bolo2meta.8
if build_rrd_subscriber
dist_man_MANS += man/bolo2rrd.8
endif
if build_redis_subscriber
dist_man_MANS += man/bolo2redis.8
endif
dist_man_MANS += man/bolo.conf.5
dist_man_MANS += man/dbolo.1
dist_man_MANS += man/dbolo.conf.5

dist_man_MANS += man/bolo-aggr.1
dist_man_MANS += man/bolo-cache.1
dist_man_MANS += man/bolo-forget.1
dist_man_MANS += man/bolo-query.1
dist_man_MANS += man/bolo-send.1

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
