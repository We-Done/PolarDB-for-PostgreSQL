#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for access/common
#
# IDENTIFICATION
#    src/backend/access/common/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/access/common
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

OBJS = \
	attmap.o \
	bufmask.o \
	detoast.o \
	heaptuple.o \
	indextuple.o \
	printsimple.o \
	printtup.o \
	relation.o \
	reloptions.o \
	scankey.o \
	session.o \
	syncscan.o \
	tidstore.o \
	toast_compression.o \
	toast_internals.o \
	tupconvert.o \
	tupdesc.o

OBJS += memtuple_px.o

include $(top_srcdir)/src/backend/common.mk
