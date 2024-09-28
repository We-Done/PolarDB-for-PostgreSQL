#-------------------------------------------------------------------------
#
# GNUmakefile--
#    Makefile for src/test/regress (the regression tests)
#
# Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
# Portions Copyright (c) 1994, Regents of the University of California
#
# src/test/regress/GNUmakefile
#
#-------------------------------------------------------------------------

PGFILEDESC = "pg_regress - test driver"
PGAPPICON = win32

subdir = src/test/regress
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

# maximum simultaneous connections for parallel tests
MAXCONNOPT =
ifdef MAX_CONNECTIONS
MAXCONNOPT += --max-connections=$(MAX_CONNECTIONS)
endif

DMA_REGRESS_OPTS = 
ifdef DMA_OPTS
DMA_REGRESS_OPTS += --dma=$(DMA_OPTS)
endif

# stuff to pass into build of pg_regress
EXTRADEFS = '-DHOST_TUPLE="$(host_tuple)"' \
	'-DSHELLPROG="$(SHELL)"'

##
## Prepare for tests
##

# Build regression test driver

all: pg_regress$(X)

pg_regress$(X): pg_regress.o pg_regress_main.o $(WIN32RES) | submake-libpgport
	$(CC) $(CFLAGS) $^ $(libpq_pgport) $(LDFLAGS) $(LDFLAGS_EX) $(LIBS) -o $@

# dependencies ensure that path changes propagate
pg_regress.o: pg_regress.c $(top_builddir)/src/port/pg_config_paths.h
pg_regress.o: override CPPFLAGS += -I$(top_builddir)/src/port -I$(libpq_srcdir) $(EXTRADEFS)

# note: because of the submake dependency, this rule's action is really a no-op
$(top_builddir)/src/port/pg_config_paths.h: | submake-libpgport
	$(MAKE) -C $(top_builddir)/src/port pg_config_paths.h

install: all installdirs
	$(INSTALL_PROGRAM) pg_regress$(X) '$(DESTDIR)$(pgxsdir)/$(subdir)/pg_regress$(X)'

installdirs:
	$(MKDIR_P) '$(DESTDIR)$(pgxsdir)/$(subdir)'

uninstall:
	rm -f '$(DESTDIR)$(pgxsdir)/$(subdir)/pg_regress$(X)'


# Build dynamically-loaded object file for CREATE FUNCTION ... LANGUAGE C.

NAME = regress
OBJS = $(WIN32RES) regress.o

include $(top_srcdir)/src/Makefile.shlib

all: all-lib

# Ensure parallel safety if a build is started in this directory
$(OBJS): | submake-libpgport submake-generated-headers


# not installed by default

regress_data_files = \
	$(wildcard $(srcdir)/sql/*.sql) \
	$(wildcard $(srcdir)/expected/*.out) \
	$(wildcard $(srcdir)/data/*.data) \
	$(srcdir)/parallel_schedule $(srcdir)/resultmap

install-tests: all install install-lib installdirs-tests
	$(MAKE) -C $(top_builddir)/contrib/spi install
	for file in $(subst $(srcdir)/,,$(regress_data_files)); do \
		$(INSTALL_DATA) $(srcdir)/$$file '$(DESTDIR)$(pkglibdir)/regress/'$$file || exit; \
	done

installdirs-tests: installdirs
	$(MKDIR_P)  $(patsubst $(srcdir)/%/,'$(DESTDIR)$(pkglibdir)/regress/%',$(sort $(dir $(regress_data_files))))


# Get some extra C modules from contrib/spi

all: refint$(DLSUFFIX) autoinc$(DLSUFFIX)

refint$(DLSUFFIX): $(top_builddir)/contrib/spi/refint$(DLSUFFIX)
	cp $< $@

autoinc$(DLSUFFIX): $(top_builddir)/contrib/spi/autoinc$(DLSUFFIX)
	cp $< $@

$(top_builddir)/contrib/spi/refint$(DLSUFFIX): | submake-contrib-spi ;

$(top_builddir)/contrib/spi/autoinc$(DLSUFFIX): | submake-contrib-spi ;

submake-contrib-spi: | submake-libpgport submake-generated-headers
	$(MAKE) -C $(top_builddir)/contrib/spi

.PHONY: submake-contrib-spi


##
## Run tests
##

<<<<<<< HEAD
REGRESS_OPTS = --dlpath=. --max-concurrent-tests=20 $(DMA_REGRESS_OPTS) $(EXTRA_REGRESS_OPTS)

check: all tablespace-setup
	echo polar_create_table_with_full_replica_identity=false > temp_config
	$(pg_regress_check) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(srcdir)/parallel_schedule $(MAXCONNOPT) $(EXTRA_TESTS)
	rm -f temp_config

check_ss_fifo: all tablespace-setup
	echo polar_create_table_with_full_replica_identity=false > temp_config
	echo polar_enable_shm_aset=true >> temp_config
	echo polar_enable_shared_server=true >> temp_config
	echo polar_ss_session_schedule_policy='fifo' >> temp_config
	$(pg_regress_check) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(srcdir)/parallel_schedule  --schedule=$(srcdir)/ss_schedule  $(MAXCONNOPT) $(EXTRA_TESTS) --polar-shared-server
	rm -f temp_config

check_ss_disposable: all tablespace-setup
	echo polar_create_table_with_full_replica_identity=false > temp_config
	echo polar_enable_shm_aset=true >> temp_config
	echo polar_enable_shared_server=true >> temp_config
	echo polar_ss_client_schedule_policy='load-balancing' >> temp_config
	echo polar_ss_session_schedule_policy='disposable' >> temp_config
	$(pg_regress_check) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(srcdir)/parallel_schedule  --schedule=$(srcdir)/ss_schedule  $(MAXCONNOPT) $(EXTRA_TESTS) --polar-shared-server
	rm -f temp_config

check_ss_dedicated: all tablespace-setup
	echo polar_create_table_with_full_replica_identity=false > temp_config
	echo polar_enable_shm_aset=true >> temp_config
	echo polar_enable_shared_server=true >> temp_config
	echo polar_ss_client_schedule_policy='random' >> temp_config
	echo polar_ss_session_schedule_policy='dedicated' >> temp_config
	$(pg_regress_check) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(srcdir)/parallel_schedule  $(MAXCONNOPT) $(EXTRA_TESTS) --polar-shared-server
	rm -f temp_config

check_ss_oom: all tablespace-setup
	echo polar_create_table_with_full_replica_identity=false > temp_config
	echo polar_enable_shm_aset=true >> temp_config
	echo polar_enable_shared_server=true >> temp_config
	echo polar_ss_client_schedule_policy='random' >> temp_config
	echo polar_ss_session_schedule_policy='disposable' >> temp_config
	echo polar_ss_backend_idle_timeout='1s' >> temp_config
	echo polar_ss_dispatcher_count='1' >> temp_config
	echo polar_ss_session_wait_timeout='1s' >> temp_config
	echo polar_ss_backend_pool_min_size='1' >> temp_config
	echo polar_ss_backend_keepalive_timeout='60s' >> temp_config
	echo polar_ss_shared_memory_size='80kB' >> temp_config
	$(pg_regress_check) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(srcdir)/parallel_schedule --schedule=$(srcdir)/ss_schedule $(MAXCONNOPT) $(EXTRA_TESTS) --polar-shared-server
	rm -f temp_config

# POLAR check
polar-check: all tablespace-setup temp-install check
	$(MKDIR_P) $(srcdir)/results/polar-dev
	$(MKDIR_P) $(srcdir)/results/polar-px-dev
	$(pg_regress_check) $(REGRESS_OPTS) --schedule=$(srcdir)/polar-dev-schedule $(MAXCONNOPT) $(EXTRA_TESTS)
=======
REGRESS_OPTS = --dlpath=. --max-concurrent-tests=20 \
	$(EXTRA_REGRESS_OPTS)

check: all
	$(pg_regress_check) $(REGRESS_OPTS) --schedule=$(srcdir)/parallel_schedule $(MAXCONNOPT) $(EXTRA_TESTS)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

check-tests: all | temp-install
	$(pg_regress_check) $(REGRESS_OPTS) $(MAXCONNOPT) $(TESTS) $(EXTRA_TESTS)

<<<<<<< HEAD
installcheck: all tablespace-setup
	echo polar_create_table_with_full_replica_identity=false > temp_config
	$(pg_regress_installcheck) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(srcdir)/serial_schedule $(EXTRA_TESTS)
	rm -f temp_config

# like installcheck, but use polar-dev-schedule
polar-installcheck: all tablespace-setup temp-install
	$(MKDIR_P) $(srcdir)/results/polar-dev
	$(MKDIR_P) $(srcdir)/results/polar-px-dev
	echo polar_create_table_with_full_replica_identity=false > temp_config
	PATH="$(bindir):$$PATH" $(pg_regress_installcheck) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(srcdir)/polar-dev-schedule $(EXTRA_TESTS)
	rm -f temp_config

# like installcheck, but use polar-dev-schedule and polar-px-dev-schedule in px mode
polar-installcheck-px: export PG_REGRESS_PX_MODE=--polar-parallel-execution --load-extension=polar_px
polar-installcheck-px: all tablespace-setup temp-install
	$(MKDIR_P) $(srcdir)/results/polar-dev
	$(MKDIR_P) $(srcdir)/results/polar-px-dev
	echo polar_create_table_with_full_replica_identity=false > temp_config
	PATH="$(bindir):$$PATH" $(pg_regress_installcheck) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(srcdir)/polar-dev-schedule --schedule=$(srcdir)/polar-px-dev-schedule $(EXTRA_TESTS) 
	rm -f temp_config


quick-installcheck: all tablespace-setup temp-install
	$(MKDIR_P) $(srcdir)/results/polar-dev
	$(MKDIR_P) $(srcdir)/results/polar-px-dev
	echo polar_create_table_with_full_replica_identity=false > temp_config
	PATH="$(bindir):$$PATH" $(pg_regress_installcheck) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(SCHEDULE) $(EXTRA_TESTS) 
	rm -f temp_config

quick-installcheck-px: export PG_REGRESS_PX_MODE=--polar-parallel-execution --load-extension=polar_px
quick-installcheck-px: all tablespace-setup temp-install
	$(MKDIR_P) $(srcdir)/results/polar-dev
	$(MKDIR_P) $(srcdir)/results/polar-px-dev
	echo polar_create_table_with_full_replica_identity=false > temp_config
	PATH="$(bindir):$$PATH" $(pg_regress_installcheck) $(REGRESS_OPTS) --temp-config=temp_config --schedule=$(SCHEDULE) $(EXTRA_TESTS) 
	rm -f temp_config
=======
installcheck: all
	$(pg_regress_installcheck) $(REGRESS_OPTS) --schedule=$(srcdir)/parallel_schedule --max-connections=1 $(EXTRA_TESTS)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

installcheck-parallel: all
	$(pg_regress_installcheck) $(REGRESS_OPTS) --schedule=$(srcdir)/parallel_schedule $(MAXCONNOPT) $(EXTRA_TESTS)

installcheck-tests: all
	$(pg_regress_installcheck) $(REGRESS_OPTS) $(TESTS) $(EXTRA_TESTS)

# old interfaces follow...

runcheck: check
runtest: installcheck
runtest-parallel: installcheck-parallel

bigtest: all
	$(pg_regress_installcheck) $(REGRESS_OPTS) --schedule=$(srcdir)/parallel_schedule --max-connections=1 numeric_big

bigcheck: all | temp-install
	$(pg_regress_check) $(REGRESS_OPTS) --schedule=$(srcdir)/parallel_schedule $(MAXCONNOPT) numeric_big


##
## Clean up
##

clean distclean: clean-lib
# things built by `all' target
	rm -f $(OBJS) refint$(DLSUFFIX) autoinc$(DLSUFFIX)
	rm -f pg_regress_main.o pg_regress.o pg_regress$(X)
# things created by various check targets
	rm -rf $(pg_regress_clean_files)
