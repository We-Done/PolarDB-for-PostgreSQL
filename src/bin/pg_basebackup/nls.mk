# src/bin/pg_basebackup/nls.mk
CATALOG_NAME     = pg_basebackup
<<<<<<< HEAD
AVAIL_LANGUAGES  = cs de es fr he it ja ko pl ru sv tr vi zh_CN
GETTEXT_FILES    = pg_basebackup.c pg_receivewal.c pg_recvlogical.c receivelog.c streamutil.c walmethods.c ../../common/fe_memutils.c ../../common/file_utils.c
GETTEXT_TRIGGERS = simple_prompt tar_set_error
=======
GETTEXT_FILES    = $(FRONTEND_COMMON_GETTEXT_FILES) \
                   astreamer_inject.c \
                   pg_basebackup.c \
                   pg_createsubscriber.c \
                   pg_receivewal.c \
                   pg_recvlogical.c \
                   receivelog.c \
                   streamutil.c \
                   walmethods.c \
                   ../../common/compression.c \
                   ../../common/controldata_utils.c \
                   ../../common/fe_memutils.c \
                   ../../common/file_utils.c \
                   ../../common/restricted_token.c \
                   ../../fe_utils/astreamer_file.c \
                   ../../fe_utils/astreamer_gzip.c \
                   ../../fe_utils/astreamer_lz4.c \
                   ../../fe_utils/astreamer_tar.c \
                   ../../fe_utils/astreamer_zstd.c \
                   ../../fe_utils/option_utils.c \
                   ../../fe_utils/recovery_gen.c \
                   ../../fe_utils/string_utils.c
GETTEXT_TRIGGERS = $(FRONTEND_COMMON_GETTEXT_TRIGGERS) simple_prompt
GETTEXT_FLAGS    = $(FRONTEND_COMMON_GETTEXT_FLAGS)
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
