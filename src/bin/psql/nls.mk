# src/bin/psql/nls.mk
CATALOG_NAME     = psql
<<<<<<< HEAD
AVAIL_LANGUAGES  = cs de es fr he it ja ko pl pt_BR ru sv tr zh_CN
GETTEXT_FILES    = command.c common.c copy.c crosstabview.c help.c input.c large_obj.c \
                   mainloop.c psqlscanslash.c startup.c \
                   describe.c sql_help.h sql_help.c \
                   tab-complete.c variables.c \
                   ../../fe_utils/print.c ../../fe_utils/psqlscan.c \
                   ../../common/exec.c ../../common/fe_memutils.c ../../common/username.c \
=======
GETTEXT_FILES    = $(FRONTEND_COMMON_GETTEXT_FILES) \
                   command.c \
                   common.c \
                   copy.c \
                   crosstabview.c \
                   help.c \
                   input.c \
                   large_obj.c \
                   mainloop.c \
                   psqlscanslash.c \
                   startup.c \
                   describe.c \
                   sql_help.h \
                   sql_help.c \
                   tab-complete.c \
                   variables.c \
                   ../../fe_utils/cancel.c \
                   ../../fe_utils/print.c \
                   ../../fe_utils/psqlscan.c \
                   ../../fe_utils/string_utils.c \
                   ../../common/exec.c \
                   ../../common/fe_memutils.c \
                   ../../common/username.c \
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
                   ../../common/wait_error.c
GETTEXT_TRIGGERS = $(FRONTEND_COMMON_GETTEXT_TRIGGERS) \
                   HELP0 HELPN N_ simple_prompt simple_prompt_extended
GETTEXT_FLAGS    = $(FRONTEND_COMMON_GETTEXT_FLAGS) \
                   HELPN:1:c-format
