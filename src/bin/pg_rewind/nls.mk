# src/bin/pg_rewind/nls.mk
CATALOG_NAME     = pg_rewind
<<<<<<< HEAD
AVAIL_LANGUAGES  = cs de es fr it ja ko pl pt_BR ru sv tr zh_CN
GETTEXT_FILES    = copy_fetch.c datapagemap.c fetch.c file_ops.c filemap.c libpq_fetch.c logging.c parsexlog.c pg_rewind.c timeline.c ../../common/fe_memutils.c ../../common/restricted_token.c xlogreader.c

GETTEXT_TRIGGERS = pg_log:2 pg_fatal report_invalid_record:2
GETTEXT_FLAGS    = pg_log:2:c-format \
    pg_fatal:1:c-format \
    report_invalid_record:2:c-format
=======
GETTEXT_FILES    = $(FRONTEND_COMMON_GETTEXT_FILES) \
                   datapagemap.c \
                   file_ops.c \
                   filemap.c \
                   libpq_source.c \
                   local_source.c \
                   parsexlog.c \
                   pg_rewind.c \
                   timeline.c \
                   xlogreader.c \
                   ../../common/controldata_utils.c \
                   ../../common/fe_memutils.c \
                   ../../common/file_utils.c \
                   ../../common/percentrepl.c \
                   ../../common/restricted_token.c \
                   ../../fe_utils/archive.c \
                   ../../fe_utils/option_utils.c \
                   ../../fe_utils/recovery_gen.c \
                   ../../fe_utils/string_utils.c
GETTEXT_TRIGGERS = $(FRONTEND_COMMON_GETTEXT_TRIGGERS) \
                   report_invalid_record:2
GETTEXT_FLAGS    = $(FRONTEND_COMMON_GETTEXT_FLAGS) \
                   report_invalid_record:2:c-format
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
