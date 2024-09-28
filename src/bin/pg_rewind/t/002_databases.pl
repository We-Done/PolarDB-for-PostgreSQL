
# Copyright (c) 2021-2024, PostgreSQL Global Development Group

use strict;
use warnings FATAL => 'all';
use PostgreSQL::Test::Utils;
use Test::More;

use FindBin;
use lib $FindBin::RealBin;

use FindBin;
use lib $FindBin::RealBin;

use RewindTest;

sub run_test
{
	my $test_mode = shift;

<<<<<<< HEAD
	RewindTest::setup_cluster($test_mode);
	RewindTest::start_master();

	# Create a database in master with a table.
	master_psql('CREATE DATABASE inmaster');
	master_psql('CREATE TABLE inmaster_tab (a int)', 'inmaster');
=======
	RewindTest::setup_cluster($test_mode, ['-g']);
	RewindTest::start_primary();

	# Create a database in primary with a table.
	primary_psql('CREATE DATABASE inprimary');
	primary_psql('CREATE TABLE inprimary_tab (a int)', 'inprimary');
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	RewindTest::create_standby($test_mode);

	# Create another database with another table, the creation is
	# replicated to the standby.
<<<<<<< HEAD
	master_psql('CREATE DATABASE beforepromotion');
	master_psql('CREATE TABLE beforepromotion_tab (a int)',
		    'beforepromotion');

	RewindTest::promote_standby();

	# Create databases in the old master and the new promoted standby.
	master_psql('CREATE DATABASE master_afterpromotion');
	master_psql('CREATE TABLE master_promotion_tab (a int)',
		    'master_afterpromotion');
	standby_psql('CREATE DATABASE standby_afterpromotion');
	standby_psql('CREATE TABLE standby_promotion_tab (a int)',
		    'standby_afterpromotion');
=======
	primary_psql('CREATE DATABASE beforepromotion');
	primary_psql('CREATE TABLE beforepromotion_tab (a int)',
		'beforepromotion');

	RewindTest::promote_standby();

	# Create databases in the old primary and the new promoted standby.
	primary_psql('CREATE DATABASE primary_afterpromotion');
	primary_psql('CREATE TABLE primary_promotion_tab (a int)',
		'primary_afterpromotion');
	standby_psql('CREATE DATABASE standby_afterpromotion');
	standby_psql('CREATE TABLE standby_promotion_tab (a int)',
		'standby_afterpromotion');
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

	# The clusters are now diverged.

	RewindTest::run_pg_rewind($test_mode);

	# Check that the correct databases are present after pg_rewind.
	check_query(
		'SELECT datname FROM pg_database ORDER BY 1',
		qq(beforepromotion
<<<<<<< HEAD
inmaster
polardb_admin
=======
inprimary
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
postgres
standby_afterpromotion
template0
template1
),
		'database names');

	# Permissions on PGDATA should have group permissions
  SKIP:
	{
		skip "unix-style permissions not supported on Windows", 1
		  if ($windows_os || $Config::Config{osname} eq 'cygwin');

<<<<<<< HEAD
		ok(check_mode_recursive($node_master->data_dir(), 0700, 0600),
=======
		ok(check_mode_recursive($node_primary->data_dir(), 0750, 0640),
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			'check PGDATA permissions');
	}

	RewindTest::clean_rewind_test();
	return;
}

# Run the test in both modes.
run_test('local');
run_test('remote');

done_testing();
