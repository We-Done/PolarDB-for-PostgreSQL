# src/bin/pg_archivecleanup/nls.mk
CATALOG_NAME     = pg_archivecleanup
<<<<<<< HEAD
AVAIL_LANGUAGES  = cs de es fr ja ko pl ru sv tr vi zh_CN
GETTEXT_FILES    = pg_archivecleanup.c
=======
GETTEXT_FILES    = $(FRONTEND_COMMON_GETTEXT_FILES) pg_archivecleanup.c ../../common/fe_memutils.c
GETTEXT_TRIGGERS = $(FRONTEND_COMMON_GETTEXT_TRIGGERS)
GETTEXT_FLAGS    = $(FRONTEND_COMMON_GETTEXT_FLAGS)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
