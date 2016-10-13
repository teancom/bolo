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

SUBS_SRC  = $(CORE_SRC)

EXTRA_DIST += README.md
EXTRA_DIST += PROTO
EXTRA_DIST += bootstrap
EXTRA_DIST += man examples

include_HEADERS = include/bolo.h include/tsdp.h
lib_LTLIBRARIES = libbolo.la libtsdp.la
noinst_LTLIBRARIES = libimpl.la
LDADD += libbolo.la

libimpl_la_SOURCES = $(SUBS_SRC)

libbolo_la_SOURCES = src/bolo_name.c \
                     src/bolo_subscriber.c \
                     src/bolo_pdu.c
libbolo_la_LDFLAGS = -version-info $(LIB_SOVERSION)

libtsdp_la_SOURCES = tsdp/internal.h \
                     tsdp/strings.c \
                     tsdp/create.c \
                     tsdp/destroy.c \
                     tsdp/extend.c \
                     tsdp/set.c \
                     tsdp/get.c \
                     tsdp/pack.c \
                     tsdp/unpack.c
libtsdp_la_LDFLAGS = -version-info $(TSDP_SOVERSION)

sbin_PROGRAMS = bolo2log bolo2meta dbolo
bolo2log_SOURCES        = src/bolo2log/main.c
bolo2meta_SOURCES       = src/bolo2meta/main.c
bolo2meta_LDADD         = $(LDADD)
dbolo_SOURCES           = src/dbolo/main.c

if build_rrd_subscriber
sbin_PROGRAMS          += bolo2rrd
bolo2rrd_SOURCES        = src/bolo2rrd/main.c
bolo2rrd_LDADD          = $(LDADD) -lrrd
endif

if build_pg_subscriber
sbin_PROGRAMS          += bolo2pg
bolo2pg_SOURCES         = src/bolo2pg/main.c
bolo2pg_LDADD           = $(LDADD) -lpq
endif

if build_sqlite_subscriber
sbin_PROGRAMS         += bolo2sqlite
bolo2sqlite_SOURCES    = src/bolo2sqlite/main.c
bolo2sqlite_LDADD      = $(LDADD) -lsqlite3
endif

if build_redis_subscriber
sbin_PROGRAMS         += bolo2redis
bolo2redis_SOURCES     = src/bolo2redis/main.c
bolo2redis_LDADD       = $(LDADD) -lhiredis
endif

if build_console_subscriber
sbin_PROGRAMS         += bolo2console
bolo2console_SOURCES   = src/bolo2console/main.c
bolo2console_LDADD     = $(LDADD) -lncursesw
endif

if build_influxdb_subscriber
sbin_PROGRAMS         += bolo2influxdb
bolo2influxdb_SOURCES  = src/bolo2influxdb/main.c
bolo2influxdb_LDADD    = $(LDADD)
endif

bin_PROGRAMS = bolo_nsca
bolo_nsca_SOURCES = src/bolo_nsca/main.c

bin_PROGRAMS += bolo
bolo_SOURCES  = src/bolo/main.c \
                src/bolo/cmd_aggregator.c   src/core.c \
                src/bolo/cmd_cache.c \
                src/bolo/cmd_forget.c \
                src/bolo/cmd_help.c \
                src/bolo/cmd_name.c \
                src/bolo/cmd_query.c \
                src/bolo/cmd_send.c \
                src/bolo/cmd_spy.c \
                src/bolo/cmd_tail.c \
                src/bolo/cmd_version.c
bolo_LDADD = $(LDADD) libimpl.la

check_SCRIPTS = t/usage t/config t/core t/broadcast t/large-payloads t/events \
                t/beacon t/bolo-send t/bolo-cache t/bolo-name \
                t/rrd-subscriber t/meta-subscriber t/log-subscriber \
                t/forget
TESTS = $(check_SCRIPTS)
dist_check_SCRIPTS=$(check_SCRIPTS) t/run t/lib

dist_man_MANS  =
dist_man_MANS += man/bolo2meta.8
dist_man_MANS += man/bolo2log.8
if build_pg_subscriber
dist_man_MANS += man/bolo2pg.8
endif
if build_rrd_subscriber
dist_man_MANS += man/bolo2rrd.8
endif
if build_redis_subscriber
dist_man_MANS += man/bolo2redis.8
endif
if build_console_subscriber
dist_man_MANS += man/bolo2console.8
endif
if build_influxdb_subscriber
dist_man_MANS += man/bolo2influxdb.8
endif
dist_man_MANS += man/bolo.conf.5
dist_man_MANS += man/bolo.1
dist_man_MANS += man/dbolo.1
dist_man_MANS += man/dbolo.conf.5

dist_man_MANS += man/bolo-aggr.1
dist_man_MANS += man/bolo-cache.1
dist_man_MANS += man/bolo-forget.1
dist_man_MANS += man/bolo-name.1
dist_man_MANS += man/bolo-query.1
dist_man_MANS += man/bolo-send.1
dist_man_MANS += man/bolo-spy.1
yaml_manpages  = $(dist_man_MANS:%=%.yml)
CLEANFILES = $(yaml_manpages)

update-website: $(yaml_manpages)
	test -d ../bolo-website
	mkdir -p ../bolo-website/data/man
	cp -a $^ ../bolo-website/data/man/

man/%.1: man/%.1.pod
	./man/build nroff $< @PACKAGE_VERSION@ > $@
man/%.1.yml: man/%.1.pod
	./man/build yml $< @PACKAGE_VERSION@ > $@

man/%.5: man/%.5.pod
	./man/build nroff $< @PACKAGE_VERSION@ > $@
man/%.5.yml: man/%.5.pod
	./man/build yml $< @PACKAGE_VERSION@ > $@

man/%.7: man/%.7.pod
	./man/build nroff $< @PACKAGE_VERSION@ > $@
man/%.7.yml: man/%.7.pod
	./man/build yml $< @PACKAGE_VERSION@ > $@

man/%.8: man/%.8.pod
	./man/build nroff $< @PACKAGE_VERSION@ > $@
man/%.8.yml: man/%.8.pod
	./man/build yml $< @PACKAGE_VERSION@ > $@

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

copyright:
	@find . -not -name config.h -name *.[ch] | xargs -r grep -L 'Copyright 20'
