# src/bin/pg_test_fsync/nls.mk
CATALOG_NAME     = pg_test_fsync
<<<<<<< HEAD
AVAIL_LANGUAGES  = cs de es fr ja ko pl ru sv tr vi zh_CN
GETTEXT_FILES    = pg_test_fsync.c
GETTEXT_TRIGGERS = die
=======
GETTEXT_FILES    = $(FRONTEND_COMMON_GETTEXT_FILES) pg_test_fsync.c ../../common/fe_memutils.c
GETTEXT_TRIGGERS = $(FRONTEND_COMMON_GETTEXT_TRIGGERS) die
GETTEXT_FLAGS    = $(FRONTEND_COMMON_GETTEXT_FLAGS)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
