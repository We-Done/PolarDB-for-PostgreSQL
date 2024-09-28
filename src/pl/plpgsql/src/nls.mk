# src/pl/plpgsql/src/nls.mk
CATALOG_NAME     = plpgsql
<<<<<<< HEAD
AVAIL_LANGUAGES  = cs de es fr it ja ko pl pt_BR ro ru sv tr vi zh_CN
GETTEXT_FILES    = pl_comp.c pl_exec.c pl_gram.c pl_funcs.c pl_handler.c pl_scanner.c
=======
GETTEXT_FILES    = pl_comp.c \
                   pl_exec.c \
                   pl_gram.c \
                   pl_funcs.c \
                   pl_handler.c \
                   pl_scanner.c
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
GETTEXT_TRIGGERS = $(BACKEND_COMMON_GETTEXT_TRIGGERS) yyerror plpgsql_yyerror
GETTEXT_FLAGS    = $(BACKEND_COMMON_GETTEXT_FLAGS)
