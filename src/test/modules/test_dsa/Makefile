# src/test/modules/test_dsa/Makefile

<<<<<<< HEAD
MODULES = test_dsa

EXTENSION = test_dsa
DATA = test_dsa--1.0.sql
PGFILEDESC = "test_dsa -- tests for DSA areas"
=======
MODULE_big = test_dsa
OBJS = \
	$(WIN32RES) \
	test_dsa.o
PGFILEDESC = "test_dsa - test code for dynamic shared memory areas"

EXTENSION = test_dsa
DATA = test_dsa--1.0.sql

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
REGRESS = test_dsa

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = src/test/modules/test_dsa
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
