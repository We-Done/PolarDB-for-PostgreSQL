# src/backend/nls.mk
CATALOG_NAME     = postgres
<<<<<<< HEAD
AVAIL_LANGUAGES  = de es fr it ja ko pl ru sv tr zh_CN
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
GETTEXT_FILES    = + gettext-files
GETTEXT_TRIGGERS = $(BACKEND_COMMON_GETTEXT_TRIGGERS) \
                   GUC_check_errmsg \
                   GUC_check_errdetail \
                   GUC_check_errhint \
                   write_stderr \
                   yyerror \
                   jsonpath_yyerror:3 \
                   parser_yyerror \
                   replication_yyerror \
                   scanner_yyerror \
                   syncrep_yyerror \
                   report_invalid_record:2 \
                   ereport_startup_progress \
                   json_token_error:2 \
                   json_manifest_parse_failure:2 \
                   error_cb:2
GETTEXT_FLAGS    = $(BACKEND_COMMON_GETTEXT_FLAGS) \
                   GUC_check_errmsg:1:c-format \
                   GUC_check_errdetail:1:c-format \
                   GUC_check_errhint:1:c-format \
                   write_stderr:1:c-format \
                   report_invalid_record:2:c-format \
                   ereport_startup_progress:1:c-format \
                   json_token_error:2:c-format \
                   error_cb:2:c-format

gettext-files: generated-parser-sources generated-headers
	find $(srcdir) $(srcdir)/../common $(srcdir)/../port -name '*.c' -print | LC_ALL=C sort >$@

my-clean:
	rm -f gettext-files

.PHONY: my-clean
clean: my-clean
