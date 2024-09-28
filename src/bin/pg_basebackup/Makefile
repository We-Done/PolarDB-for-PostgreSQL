#-------------------------------------------------------------------------
#
# Makefile for src/bin/pg_basebackup
#
# Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
# Portions Copyright (c) 1994, Regents of the University of California
#
# src/bin/pg_basebackup/Makefile
#
#-------------------------------------------------------------------------

PGFILEDESC = "pg_basebackup/pg_receivewal/pg_recvlogical - streaming WAL and backup receivers"
PGAPPICON=win32

EXTRA_INSTALL=contrib/test_decoding

subdir = src/bin/pg_basebackup
top_builddir = ../../..
tmp_bindir = $(abs_top_builddir)/tmp_install/$(bindir)
include $(top_builddir)/src/Makefile.global

# make these available to TAP test scripts
export LZ4
export TAR
# Note that GZIP cannot be used directly as this environment variable is
# used by the command "gzip" to pass down options, so stick with a different
# name.
export GZIP_PROGRAM=$(GZIP)

override CPPFLAGS := -I$(libpq_srcdir) $(CPPFLAGS)
LDFLAGS_INTERNAL += -Wl,--as-needed -L$(top_builddir)/src/fe_utils -lpgfeutils $(libpq_pgport)

ifeq ($(with_pfsd),yes)
LDFLAGS_INTERNAL += -L/usr/local/polarstore/pfsd/lib/ -lpfsd
endif

OBJS = \
	$(WIN32RES) \
	receivelog.o \
	streamutil.o \
	walmethods.o

<<<<<<< HEAD
all: polar_basebackup pg_receivewal pg_recvlogical

polar_basebackup: pg_basebackup.o $(OBJS) | submake-libpq submake-libpgport submake-libpgfeutils
	$(CC) $(CFLAGS) pg_basebackup.o $(OBJS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)
	rm -f pg_basebackup$(X)
	$(LN_S) polar_basebackup$(X) pg_basebackup$(X)
=======
BBOBJS = \
	pg_basebackup.o \
	astreamer_inject.o

all: pg_basebackup pg_createsubscriber pg_receivewal pg_recvlogical

pg_basebackup: $(BBOBJS) $(OBJS) | submake-libpq submake-libpgport submake-libpgfeutils
	$(CC) $(CFLAGS) $(BBOBJS) $(OBJS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)

pg_createsubscriber: pg_createsubscriber.o $(WIN32RES) | submake-libpq submake-libpgport submake-libpgfeutils
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

pg_receivewal: pg_receivewal.o $(OBJS) | submake-libpq submake-libpgport submake-libpgfeutils
	$(CC) $(CFLAGS) pg_receivewal.o $(OBJS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)

pg_recvlogical: pg_recvlogical.o $(OBJS) | submake-libpq submake-libpgport submake-libpgfeutils
	$(CC) $(CFLAGS) pg_recvlogical.o $(OBJS) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@$(X)

install: all installdirs
<<<<<<< HEAD
	$(INSTALL_PROGRAM) polar_basebackup$(X) '$(DESTDIR)$(bindir)/polar_basebackup$(X)'
=======
	$(INSTALL_PROGRAM) pg_basebackup$(X) '$(DESTDIR)$(bindir)/pg_basebackup$(X)'
	$(INSTALL_PROGRAM) pg_createsubscriber$(X) '$(DESTDIR)$(bindir)/pg_createsubscriber$(X)'
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	$(INSTALL_PROGRAM) pg_receivewal$(X) '$(DESTDIR)$(bindir)/pg_receivewal$(X)'
	$(INSTALL_PROGRAM) pg_recvlogical$(X) '$(DESTDIR)$(bindir)/pg_recvlogical$(X)'
	cp -frp pg_basebackup$(X) '$(DESTDIR)$(bindir)/pg_basebackup$(X)'

installdirs:
	$(MKDIR_P) '$(DESTDIR)$(bindir)'

uninstall:
<<<<<<< HEAD
	rm -f '$(DESTDIR)$(bindir)/polar_basebackup$(X)'
=======
	rm -f '$(DESTDIR)$(bindir)/pg_basebackup$(X)'
	rm -f '$(DESTDIR)$(bindir)/pg_createsubscriber$(X)'
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
	rm -f '$(DESTDIR)$(bindir)/pg_receivewal$(X)'
	rm -f '$(DESTDIR)$(bindir)/pg_recvlogical$(X)'
	rm -f '$(DESTDIR)$(bindir)/pg_basebackup$(X)'

<<<<<<< HEAD
clean distclean maintainer-clean:
	rm -f polar_basebackup$(X) pg_receivewal$(X) pg_recvlogical$(X) \
		pg_basebackup.o pg_receivewal.o pg_recvlogical.o \
=======
clean distclean:
	rm -f pg_basebackup$(X) pg_createsubscriber$(X) pg_receivewal$(X) pg_recvlogical$(X) \
		$(BBOBJS) pg_createsubscriber.o pg_receivewal.o pg_recvlogical.o \
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		$(OBJS)
	rm -rf tmp_check
	rm -f pg_basebackup

check:
	$(LN_S) $(tmp_bindir)/polar_basebackup $(tmp_bindir)/pg_basebackup
	$(prove_check)

installcheck:
	$(prove_installcheck)
