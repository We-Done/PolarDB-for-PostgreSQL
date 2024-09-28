# src/bin/pg_waldump/nls.mk
CATALOG_NAME     = pg_waldump
<<<<<<< HEAD
AVAIL_LANGUAGES  = cs de es fr ja ko ru sv tr vi zh_CN
GETTEXT_FILES    = pg_waldump.c
GETTEXT_TRIGGERS = fatal_error
GETTEXT_FLAGS    = fatal_error:1:c-format
=======
GETTEXT_FILES    = $(FRONTEND_COMMON_GETTEXT_FILES) \
                   pg_waldump.c \
                   xlogreader.c \
                   xlogstats.c \
                   ../../common/fe_memutils.c \
                   ../../common/file_utils.c
GETTEXT_TRIGGERS = $(FRONTEND_COMMON_GETTEXT_TRIGGERS) \
                   report_invalid_record:2
GETTEXT_FLAGS    = $(FRONTEND_COMMON_GETTEXT_FLAGS) \
                   report_invalid_record:2:c-format
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
