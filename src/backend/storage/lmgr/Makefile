#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for storage/lmgr
#
# IDENTIFICATION
#    src/backend/storage/lmgr/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/storage/lmgr
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

<<<<<<< HEAD
OBJS = lmgr.o lock.o proc.o deadlock.o lwlock.o lwlocknames.o spin.o \
	s_lock.o predicate.o condition_variable.o polar_lock_stats.o
=======
OBJS = \
	condition_variable.o \
	deadlock.o \
	lmgr.o \
	lock.o \
	lwlock.o \
	predicate.o \
	proc.o \
	s_lock.o \
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

include $(top_srcdir)/src/backend/common.mk

ifdef TAS
TASPATH = $(top_builddir)/src/backend/port/tas.o
endif

<<<<<<< HEAD
# POLAR: use -march=armv8-a+lse if possible
s_lock.o: CFLAGS+=$(POLAR_CFLAGS_ARMV8_LSE)
# POLAR end

s_lock_test: s_lock.c $(top_builddir)/src/port/libpgport.a
=======
s_lock_test: s_lock.c $(top_builddir)/src/common/libpgcommon.a $(top_builddir)/src/port/libpgport.a
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	$(CC) $(CPPFLAGS) $(CFLAGS) -DS_LOCK_TEST=1 $(srcdir)/s_lock.c \
		$(TASPATH) -L $(top_builddir)/src/common -lpgcommon \
		-L $(top_builddir)/src/port -lpgport -lm -o s_lock_test

lwlocknames.h: ../../../include/storage/lwlocklist.h ../../utils/activity/wait_event_names.txt generate-lwlocknames.pl
	$(PERL) $(srcdir)/generate-lwlocknames.pl $^

check: s_lock_test
	./s_lock_test

clean:
	rm -f s_lock_test
	rm -f lwlocknames.h
