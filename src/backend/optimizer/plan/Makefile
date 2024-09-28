#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for optimizer/plan
#
# IDENTIFICATION
#    src/backend/optimizer/plan/Makefile
#
#-------------------------------------------------------------------------

subdir = src/backend/optimizer/plan
top_builddir = ../../../..
include $(top_builddir)/src/Makefile.global

<<<<<<< HEAD
OBJS = analyzejoins.o createplan.o initsplan.o planagg.o planmain.o planner.o \
	setrefs.o subselect.o polar_or_to_union_all.o

ifeq ($(enable_polar_px),yes)
OBJS += px_opt.o
endif
=======
OBJS = \
	analyzejoins.o \
	createplan.o \
	initsplan.o \
	planagg.o \
	planmain.o \
	planner.o \
	setrefs.o \
	subselect.o
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

include $(top_srcdir)/src/backend/common.mk
