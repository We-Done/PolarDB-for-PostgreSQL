# src/bin/pg_resetwal/nls.mk
CATALOG_NAME     = pg_resetwal
<<<<<<< HEAD
AVAIL_LANGUAGES  = cs de es fr ja ko ru sv tr zh_CN
GETTEXT_FILES    = pg_resetwal.c ../../common/restricted_token.c
=======
GETTEXT_FILES    = $(FRONTEND_COMMON_GETTEXT_FILES) \
                   pg_resetwal.c \
                   ../../common/controldata_utils.c \
                   ../../common/fe_memutils.c \
                   ../../common/file_utils.c \
                   ../../common/restricted_token.c \
                   ../../fe_utils/option_utils.c
GETTEXT_TRIGGERS = $(FRONTEND_COMMON_GETTEXT_TRIGGERS)
GETTEXT_FLAGS    = $(FRONTEND_COMMON_GETTEXT_FLAGS)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
