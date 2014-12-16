# Makefile.am

AUTOMAKE_OPTIONS = foreign subdir-objects
ACLOCAL_AMFLAGS = -I build

AM_CFLAGS =
AM_CFLAGS += -Wall
AM_CFLAGS += -g
AM_CFLAGS += @GCOV_CFLAGS@

CORE_SRC  =
CORE_SRC += src/bolo.h
CORE_SRC += src/wire.h src/wire.c

sbin_PROGRAMS = bolo
bolo_SOURCES = $(CORE_SRC) src/daemon.c
