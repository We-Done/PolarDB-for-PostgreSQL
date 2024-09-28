# src/test/modules/Makefile

subdir = src/test/modules
top_builddir = ../../..
include $(top_builddir)/src/Makefile.global

SUBDIRS = \
		  brin \
		  commit_ts \
		  delay_execution \
		  dummy_index_am \
		  dummy_seclabel \
<<<<<<< HEAD
		  snapshot_too_old \
		  test_async_ddl_lock_replay \
		  test_bloomfilter \
		  test_buffer \
		  test_cancel_key \
		  test_csn \
		  test_csn_xact \
=======
		  libpq_pipeline \
		  plsample \
		  spgist_name_ops \
		  test_bloomfilter \
		  test_copy_callbacks \
		  test_custom_rmgrs \
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		  test_ddl_deparse \
		  test_dsa \
		  test_dsm_registry \
		  test_extensions \
<<<<<<< HEAD
		  test_logindex \
		  test_multi_version_snapshot \
=======
		  test_ginpostinglist \
		  test_integerset \
		  test_json_parser \
		  test_lfind \
		  test_misc \
		  test_oat_hooks \
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
		  test_parser \
		  test_pbp \
		  test_pg_dump \
		  test_polar_datamax \
		  test_polar_directio \
		  test_predtest \
		  test_radixtree \
		  test_rbtree \
<<<<<<< HEAD
		  test_read_core_pattern \
		  test_rls_hooks \
		  test_shm_mq \
		  test_slru \
		  test_xids_snapshot_mvcc \
		  worker_spi \
		  test_polar_bulk_read \
		  test_px \
		  test_wal_pipeline \
		  test_flashback_log \
		  test_flashback_table \
		  test_dsa	\
		  test_polar_shm_aset

ifeq ($(enable_inject_faults),yes)
SUBDIRS += test_xact_split
=======
		  test_regex \
		  test_resowner \
		  test_rls_hooks \
		  test_shm_mq \
		  test_slru \
		  test_tidstore \
		  unsafe_tests \
		  worker_spi \
		  xid_wraparound


ifeq ($(enable_injection_points),yes)
SUBDIRS += injection_points gin
else
ALWAYS_SUBDIRS += injection_points gin
endif

ifeq ($(with_ssl),openssl)
SUBDIRS += ssl_passphrase_callback
else
ALWAYS_SUBDIRS += ssl_passphrase_callback
endif

# Test runs an LDAP server, so only run if ldap is in PG_TEST_EXTRA
ifeq ($(with_ldap),yes)
ifneq (,$(filter ldap,$(PG_TEST_EXTRA)))
SUBDIRS += ldap_password_func
else
ALWAYS_SUBDIRS += ldap_password_func
endif
else
ALWAYS_SUBDIRS += ldap_password_func
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
endif

$(recurse)
$(recurse_always)
