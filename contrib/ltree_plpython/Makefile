# contrib/ltree_plpython/Makefile

MODULE_big = ltree_plpython$(python_majorversion)
OBJS = \
	$(WIN32RES) \
	ltree_plpython.o
PGFILEDESC = "ltree_plpython - ltree transform for plpython"

<<<<<<< HEAD
EXTENSION = ltree_plpythonu ltree_plpython2u ltree_plpython3u
DATA = ltree_plpythonu--1.0.sql ltree_plpython2u--1.0.sql ltree_plpython3u--1.0.sql
=======
EXTENSION = ltree_plpython3u
DATA = ltree_plpython3u--1.0.sql
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

REGRESS = ltree_plpython

PG_CPPFLAGS = $(python_includespec) -DPLPYTHON_LIBNAME='"plpython$(python_majorversion)"'

PG_CPPFLAGS = $(python_includespec) -DPLPYTHON_LIBNAME='"plpython$(python_majorversion)"'

ifdef USE_PGXS
PG_CPPFLAGS += -I$(includedir_server)/extension
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
PG_CPPFLAGS += -I$(top_srcdir)/src/pl/plpython -I$(top_srcdir)/contrib
subdir = contrib/ltree_plpython
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

# We must link libpython explicitly
ifeq ($(PORTNAME), win32)
# ... see silliness in plpython Makefile ...
SHLIB_LINK_INTERNAL += $(sort $(wildcard ../../src/pl/plpython/libpython*.a))
else
rpathdir = $(python_libdir)
SHLIB_LINK += $(python_libspec) $(python_additional_libs)
endif

REGRESS_OPTS += --load-extension=ltree
EXTRA_INSTALL += contrib/ltree
