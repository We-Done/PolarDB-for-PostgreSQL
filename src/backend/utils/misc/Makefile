#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for utils/misc
#
# IDENTIFICATION
#    src/backend/utils/misc/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/utils/misc
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

override CPPFLAGS := -I. -I$(srcdir) $(CPPFLAGS)

<<<<<<< HEAD
OBJS = backend_random.o guc.o help_config.o pg_config.o pg_controldata.o \
       pg_rusage.o ps_status.o queryenvironment.o rls.o sampling.o \
       superuser.o timeout.o tzparser.o polar_sql_time_stat.o \
       guc_px.o faultinjector.o
=======
OBJS = \
	conffiles.o \
	guc.o \
	guc-file.o \
	guc_funcs.o \
	guc_tables.o \
	help_config.o \
	injection_point.o \
	pg_config.o \
	pg_controldata.o \
	pg_rusage.o \
	ps_status.o \
	queryenvironment.o \
	rls.o \
	sampling.o \
	superuser.o \
	timeout.o \
	tzparser.o
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

# This location might depend on the installation directories. Therefore
# we can't substitute it into pg_config.h.
ifdef krb_srvtab
override CPPFLAGS += -DPG_KRB_SRVTAB='"$(krb_srvtab)"'
endif

include $(top_srcdir)/src/backend/common.mk

<<<<<<< HEAD
# guc-file is compiled as part of guc
guc.o: guc-file.c

guc_px.o: $(top_srcdir)/src/include/utils/px_sync_guc_name.h $(top_srcdir)/src/include/utils/px_unsync_guc_name.h

# Note: guc-file.c is not deleted by 'make clean',
# since we want to ship it in distribution tarballs.
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
clean:
	rm -f guc-file.c
