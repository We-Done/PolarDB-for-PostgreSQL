<<<<<<< HEAD
=======

# Copyright (c) 2021-2024, PostgreSQL Global Development Group

>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
# Test for replication slot limit
# Ensure that max_slot_wal_keep_size limits the number of WAL files to
# be kept by replication slots.
use strict;
<<<<<<< HEAD
use warnings;

use TestLib;
use PostgresNode;

use File::Path qw(rmtree);
use Test::More tests => 14;
use Time::HiRes qw(usleep);
use threads;

$ENV{PGDATABASE} = 'postgres';

# Initialize primary node, setting wal-segsize to 1MB
my $node_primary = get_new_node('primary');
=======
use warnings FATAL => 'all';

use PostgreSQL::Test::Utils;
use PostgreSQL::Test::Cluster;
use Test::More;
use Time::HiRes qw(usleep);

# Initialize primary node, setting wal-segsize to 1MB
my $node_primary = PostgreSQL::Test::Cluster->new('primary');
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
$node_primary->init(allows_streaming => 1, extra => ['--wal-segsize=1']);
$node_primary->append_conf(
	'postgresql.conf', qq(
min_wal_size = 2MB
max_wal_size = 4MB
log_checkpoints = yes
<<<<<<< HEAD
polar_logindex_mem_size = 0
polar_enable_max_slot_wal_keep_size = yes
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
));
$node_primary->start;
$node_primary->safe_psql('postgres',
	"SELECT pg_create_physical_replication_slot('rep1')");

<<<<<<< HEAD
$node_primary->safe_psql('postgres',
	"create extension polar_monitor");

# The slot state and remain should be null before the first connection
my $result = $node_primary->safe_psql('postgres',
	"SELECT restart_lsn IS NULL, wal_status is NULL, safe_wal_size is NULL FROM polar_replication_slots WHERE slot_name = 'rep1'"
=======
# The slot state and remain should be null before the first connection
my $result = $node_primary->safe_psql('postgres',
	"SELECT restart_lsn IS NULL, wal_status is NULL, safe_wal_size is NULL FROM pg_replication_slots WHERE slot_name = 'rep1'"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
);
is($result, "t|t|t", 'check the state of non-reserved slot is "unknown"');


# Take backup
my $backup_name = 'my_backup';
$node_primary->backup($backup_name);

# Create a standby linking to it using the replication slot
<<<<<<< HEAD
my $node_standby = get_new_node('standby_1');
$node_standby->init_from_backup($node_primary, $backup_name,
	has_streaming => 1);
$node_standby->append_conf('recovery.conf', "primary_slot_name = 'rep1'");
=======
my $node_standby = PostgreSQL::Test::Cluster->new('standby_1');
$node_standby->init_from_backup($node_primary, $backup_name,
	has_streaming => 1);
$node_standby->append_conf('postgresql.conf', "primary_slot_name = 'rep1'");
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

$node_standby->start;

# Wait until standby has replayed enough data
<<<<<<< HEAD
my $start_lsn = $node_primary->lsn('write');
$node_primary->wait_for_catchup($node_standby, 'replay', $start_lsn);
=======
$node_primary->wait_for_catchup($node_standby);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

# Stop standby
$node_standby->stop;

# Preparation done, the slot is the state "reserved" now
$result = $node_primary->safe_psql('postgres',
<<<<<<< HEAD
	"SELECT wal_status, safe_wal_size IS NULL FROM polar_replication_slots WHERE slot_name = 'rep1'"
=======
	"SELECT wal_status, safe_wal_size IS NULL FROM pg_replication_slots WHERE slot_name = 'rep1'"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
);
is($result, "reserved|t", 'check the catching-up state');

# Advance WAL by five segments (= 5MB) on primary
<<<<<<< HEAD
advance_wal($node_primary, 1);
=======
$node_primary->advance_wal(1);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
$node_primary->safe_psql('postgres', "CHECKPOINT;");

# The slot is always "safe" when fitting max_wal_size
$result = $node_primary->safe_psql('postgres',
<<<<<<< HEAD
	"SELECT wal_status, safe_wal_size IS NULL FROM polar_replication_slots WHERE slot_name = 'rep1'"
=======
	"SELECT wal_status, safe_wal_size IS NULL FROM pg_replication_slots WHERE slot_name = 'rep1'"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
);
is($result, "reserved|t",
	'check that it is safe if WAL fits in max_wal_size');

<<<<<<< HEAD
advance_wal($node_primary, 4);
=======
$node_primary->advance_wal(4);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
$node_primary->safe_psql('postgres', "CHECKPOINT;");

# The slot is always "safe" when max_slot_wal_keep_size is not set
$result = $node_primary->safe_psql('postgres',
<<<<<<< HEAD
	"SELECT wal_status, safe_wal_size IS NULL FROM polar_replication_slots WHERE slot_name = 'rep1'"
=======
	"SELECT wal_status, safe_wal_size IS NULL FROM pg_replication_slots WHERE slot_name = 'rep1'"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
);
is($result, "reserved|t", 'check that slot is working');

# The standby can reconnect to primary
$node_standby->start;

<<<<<<< HEAD
$start_lsn = $node_primary->lsn('write');
$node_primary->wait_for_catchup($node_standby, 'replay', $start_lsn);
=======
$node_primary->wait_for_catchup($node_standby);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

$node_standby->stop;

# Set max_slot_wal_keep_size on primary
my $max_slot_wal_keep_size_mb = 6;
$node_primary->append_conf(
	'postgresql.conf', qq(
max_slot_wal_keep_size = ${max_slot_wal_keep_size_mb}MB
<<<<<<< HEAD
polar_enable_max_slot_wal_keep_size = yes
=======
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
));
$node_primary->reload;

# The slot is in safe state.

$result = $node_primary->safe_psql('postgres',
<<<<<<< HEAD
	"SELECT wal_status FROM polar_replication_slots WHERE slot_name = 'rep1'");
is($result, "reserved", 'check that max_slot_wal_keep_size is working');

# Advance WAL again then checkpoint, reducing remain by 2 MB.
advance_wal($node_primary, 2);
=======
	"SELECT wal_status FROM pg_replication_slots WHERE slot_name = 'rep1'");
is($result, "reserved", 'check that max_slot_wal_keep_size is working');

# Advance WAL again then checkpoint, reducing remain by 2 MB.
$node_primary->advance_wal(2);
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
$node_primary->safe_psql('postgres', "CHECKPOINT;");

# The slot is still working
$result = $node_primary->safe_psql('postgres',
<<<<<<< HEAD
	"SELECT wal_status FROM polar_replication_slots WHERE slot_name = 'rep1'");
=======
	"SELECT wal_status FROM pg_replication_slots WHERE slot_name = 'rep1'");
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
is($result, "reserved",
	'check that safe_wal_size gets close to the current LSN');

# The standby can reconnect to primary
$node_standby->start;
<<<<<<< HEAD
$start_lsn = $node_primary->lsn('write');
$node_primary->wait_for_catchup($node_standby, 'replay', $start_lsn);
$node_standby->stop;

# The standby can reconnect to primary
$node_standby->start;
$start_lsn = $node_primary->lsn('write');
$node_primary->wait_for_catchup($node_standby, 'replay', $start_lsn);
$node_standby->stop;

# Advance WAL again without checkpoint, reducing remain by 6 MB.
advance_wal($node_primary, 6);

# Slot gets into 'reserved' state
$result = $node_primary->safe_psql('postgres',
	"SELECT wal_status FROM polar_replication_slots WHERE slot_name = 'rep1'");
=======
$node_primary->wait_for_catchup($node_standby);
$node_standby->stop;

# wal_keep_size overrides max_slot_wal_keep_size
$result = $node_primary->safe_psql('postgres',
	"ALTER SYSTEM SET wal_keep_size to '8MB'; SELECT pg_reload_conf();");
# Advance WAL again then checkpoint, reducing remain by 6 MB.
$node_primary->advance_wal(6);
$result = $node_primary->safe_psql('postgres',
	"SELECT wal_status as remain FROM pg_replication_slots WHERE slot_name = 'rep1'"
);
is($result, "extended",
	'check that wal_keep_size overrides max_slot_wal_keep_size');
# restore wal_keep_size
$result = $node_primary->safe_psql('postgres',
	"ALTER SYSTEM SET wal_keep_size to 0; SELECT pg_reload_conf();");

# The standby can reconnect to primary
$node_standby->start;
$node_primary->wait_for_catchup($node_standby);
$node_standby->stop;

# Advance WAL again without checkpoint, reducing remain by 6 MB.
$node_primary->advance_wal(6);

# Slot gets into 'reserved' state
$result = $node_primary->safe_psql('postgres',
	"SELECT wal_status FROM pg_replication_slots WHERE slot_name = 'rep1'");
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
is($result, "extended", 'check that the slot state changes to "extended"');

# do checkpoint so that the next checkpoint runs too early
$node_primary->safe_psql('postgres', "CHECKPOINT;");

# Advance WAL again without checkpoint; remain goes to 0.
<<<<<<< HEAD
advance_wal($node_primary, 1);

# Slot gets into 'unreserved' state and safe_wal_size is negative
$result = $node_primary->safe_psql('postgres',
	"SELECT wal_status, safe_wal_size <= 0 FROM polar_replication_slots WHERE slot_name = 'rep1'"
=======
$node_primary->advance_wal(1);

# Slot gets into 'unreserved' state and safe_wal_size is negative
$result = $node_primary->safe_psql('postgres',
	"SELECT wal_status, safe_wal_size <= 0 FROM pg_replication_slots WHERE slot_name = 'rep1'"
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
);
is($result, "unreserved|t",
	'check that the slot state changes to "unreserved"');

# The standby still can connect to primary before a checkpoint
$node_standby->start;

<<<<<<< HEAD
$start_lsn = $node_primary->lsn('write');
$node_primary->wait_for_catchup($node_standby, 'replay', $start_lsn);

$node_standby->stop;

ok( !find_in_log(
		$node_standby,
		"requested WAL segment [0-9A-F]+ has already been removed"),
	'check that required WAL segments are still available');

# Advance WAL again, the slot loses the oldest segment.
my $logstart = get_log_size($node_primary);
advance_wal($node_primary, 7);
$node_primary->safe_psql('postgres', "CHECKPOINT;");

# WARNING should be issued
ok( find_in_log(
		$node_primary,
		"invalidating slot \"rep1\" because its restart_lsn [0-9A-F/]+ exceeds max_slot_wal_keep_size",
		$logstart),
	'check that the warning is logged');

# This slot should be broken
$result = $node_primary->safe_psql('postgres',
	"SELECT slot_name, active, restart_lsn IS NULL, wal_status, safe_wal_size FROM polar_replication_slots WHERE slot_name = 'rep1'"
);
is($result, "rep1|f|t|lost|",
	'check that the slot became inactive and the state "lost" persists');

# The standby no longer can connect to the primary
$logstart = get_log_size($node_standby);
$node_standby->start;

my $failed = 0;
for (my $i = 0; $i < 10000; $i++)
{
	if (find_in_log(
			$node_standby,
=======
$node_primary->wait_for_catchup($node_standby);

$node_standby->stop;

ok( !$node_standby->log_contains(
		"requested WAL segment [0-9A-F]+ has already been removed"),
	'check that required WAL segments are still available');

# Create one checkpoint, to improve stability of the next steps
$node_primary->safe_psql('postgres', "CHECKPOINT;");

# Prevent other checkpoints from occurring while advancing WAL segments
$node_primary->safe_psql('postgres',
	"ALTER SYSTEM SET max_wal_size='40MB'; SELECT pg_reload_conf()");

# Advance WAL again. The slot loses the oldest segment by the next checkpoint
my $logstart = -s $node_primary->logfile;
$node_primary->advance_wal(7);

# Now create another checkpoint and wait until the WARNING is issued
$node_primary->safe_psql('postgres',
	'ALTER SYSTEM RESET max_wal_size; SELECT pg_reload_conf()');
$node_primary->safe_psql('postgres', "CHECKPOINT;");
my $invalidated = 0;
for (my $i = 0; $i < 10 * $PostgreSQL::Test::Utils::timeout_default; $i++)
{
	if ($node_primary->log_contains(
			'invalidating obsolete replication slot "rep1"', $logstart))
	{
		$invalidated = 1;
		last;
	}
	usleep(100_000);
}
ok($invalidated, 'check that slot invalidation has been logged');

$result = $node_primary->safe_psql(
	'postgres',
	qq[
	SELECT slot_name, active, restart_lsn IS NULL, wal_status, safe_wal_size
	FROM pg_replication_slots WHERE slot_name = 'rep1']);
is($result, "rep1|f|t|lost|",
	'check that the slot became inactive and the state "lost" persists');

# Wait until current checkpoint ends
my $checkpoint_ended = 0;
for (my $i = 0; $i < 10 * $PostgreSQL::Test::Utils::timeout_default; $i++)
{
	if ($node_primary->log_contains("checkpoint complete: ", $logstart))
	{
		$checkpoint_ended = 1;
		last;
	}
	usleep(100_000);
}
ok($checkpoint_ended, 'waited for checkpoint to end');

# The invalidated slot shouldn't keep the old-segment horizon back;
# see bug #17103: https://postgr.es/m/17103-004130e8f27782c9@postgresql.org
# Test for this by creating a new slot and comparing its restart LSN
# to the oldest existing file.
my $redoseg = $node_primary->safe_psql('postgres',
	"SELECT pg_walfile_name(lsn) FROM pg_create_physical_replication_slot('s2', true)"
);
my $oldestseg = $node_primary->safe_psql('postgres',
	"SELECT pg_ls_dir AS f FROM pg_ls_dir('pg_wal') WHERE pg_ls_dir ~ '^[0-9A-F]{24}\$' ORDER BY 1 LIMIT 1"
);
$node_primary->safe_psql('postgres',
	qq[SELECT pg_drop_replication_slot('s2')]);
is($oldestseg, $redoseg, "check that segments have been removed");

# The standby no longer can connect to the primary
$logstart = -s $node_standby->logfile;
$node_standby->start;

my $failed = 0;
for (my $i = 0; $i < 10 * $PostgreSQL::Test::Utils::timeout_default; $i++)
{
	if ($node_standby->log_contains(
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
			"requested WAL segment [0-9A-F]+ has already been removed",
			$logstart))
	{
		$failed = 1;
		last;
	}
	usleep(100_000);
}
ok($failed, 'check that replication has been broken');

<<<<<<< HEAD
$node_primary->stop('immediate');
$node_standby->stop('immediate');

my $node_primary2 = get_new_node('primary2');
$node_primary2->init(allows_streaming => 1);
$node_primary2->append_conf(
	'postgresql.conf', qq(
min_wal_size = 2GB
max_wal_size = 4GB
=======
$node_primary->stop;
$node_standby->stop;

my $node_primary2 = PostgreSQL::Test::Cluster->new('primary2');
$node_primary2->init(allows_streaming => 1);
$node_primary2->append_conf(
	'postgresql.conf', qq(
min_wal_size = 32MB
max_wal_size = 32MB
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
log_checkpoints = yes
));
$node_primary2->start;
$node_primary2->safe_psql('postgres',
	"SELECT pg_create_physical_replication_slot('rep1')");
$backup_name = 'my_backup2';
$node_primary2->backup($backup_name);

$node_primary2->stop;
$node_primary2->append_conf(
	'postgresql.conf', qq(
max_slot_wal_keep_size = 0
<<<<<<< HEAD
polar_enable_max_slot_wal_keep_size = yes
));
$node_primary2->start;

$node_standby = get_new_node('standby_2');
$node_standby->init_from_backup($node_primary2, $backup_name,
	has_streaming => 1);
$node_standby->append_conf('recovery.conf', "primary_slot_name = 'rep1'");
$node_standby->start;
my @result =
  split(
	'\n',
	$node_primary2->safe_psql(
		'postgres',
		"CREATE TABLE tt();
		 DROP TABLE tt;
		 SELECT pg_switch_wal();
		 CHECKPOINT;
		 SELECT 'finished';",
		timeout => '60'));
is($result[1], 'finished', 'check if checkpoint command is not blocked');

$node_primary2->stop('immediate');
$node_standby->stop('immediate');

my $node_primary3 = get_new_node('primary3');
$node_primary3->init(allows_streaming => 1, extra => ['--wal-segsize=32']);
$node_primary3->append_conf(
	'postgresql.conf', qq(
min_wal_size = 64MB
max_wal_size = 1000MB
max_slot_wal_keep_size = 100MB
polar_enable_max_slot_wal_keep_size = yes
log_checkpoints = yes
polar_logindex_mem_size = 0
wal_level = logical
));
my $logstart = get_log_size($node_primary3);
$node_primary3->start;
$node_primary3->psql('postgres', "SELECT * FROM pg_create_logical_replication_slot('rs1', 'test_decoding')");
$node_primary3->psql('postgres', "create table t1(i int); insert into t1 select generate_series(1,8000000);");
test_async_execute($node_primary3, "SELECT * FROM pg_logical_slot_get_changes('rs1', NULL, NULL);", 0);
sleep 1;
$node_primary3->psql('postgres', "checkpoint;");

my $failed = 0;

print "node_primary3: $node_primary3\n";
print "logstart: $logstart\n";
print "logfile: $node_primary3->logstart\n";

for (my $i = 0; $i < 10000; $i++)
{
	if (find_in_log(
			$node_primary3,
			"terminating process [0-9]+ because replication slot",
			$logstart))
	{
		$failed = 1;
		last;
	}
	usleep(100_000);
}
ok($failed, 'check that replication has been broken');

#####################################
# Advance WAL of $node by $n segments
sub advance_wal
{
	my ($node, $n) = @_;

	# Advance by $n segments (= (16 * $n) MB) on primary
	for (my $i = 0; $i < $n; $i++)
	{
		$node->safe_psql('postgres',
			"CREATE TABLE t (); DROP TABLE t; SELECT pg_switch_wal();");
	}
	return;
}

# return the size of logfile of $node in bytes
sub get_log_size
{
	my ($node) = @_;

	return (stat $node->logfile)[7];
}

# find $pat in logfile of $node after $off-th byte
sub find_in_log
{
	my ($node, $pat, $off) = @_;

	$off = 0 unless defined $off;
	my $log = TestLib::slurp_file($node->logfile);
	return 0 if (length($log) <= $off);

	$log = substr($log, $off);

	return $log =~ m/$pat/;
}

sub test_async_execute
{
	my $node	= shift;
	my $sql		= shift;
	my $timeout	= shift;

	my $pid = fork();

	if( $pid == 0) {
		$ENV{"whoami"} = "child";
		my $start_time = time();
		$node->psql('postgres', $sql);
		my $stop_time = time();
		if ($timeout != 0) {
			if ($stop_time - $start_time > $timeout + 1){
				exit -1;
			} 
		}
		else {
			print "no check time";
		}
		exit 0;
	}
}
=======
));
$node_primary2->start;

$node_standby = PostgreSQL::Test::Cluster->new('standby_2');
$node_standby->init_from_backup($node_primary2, $backup_name,
	has_streaming => 1);
$node_standby->append_conf('postgresql.conf', "primary_slot_name = 'rep1'");
$node_standby->start;
$node_primary2->advance_wal(1);
$result = $node_primary2->safe_psql(
	'postgres',
	"CHECKPOINT; SELECT 'finished';",
	timeout => $PostgreSQL::Test::Utils::timeout_default);
is($result, 'finished', 'check if checkpoint command is not blocked');

$node_primary2->stop;
$node_standby->stop;

# The next test depends on Perl's `kill`, which apparently is not
# portable to Windows.  (It would be nice to use Test::More's `subtest`,
# but that's not in the ancient version we require.)
if ($PostgreSQL::Test::Utils::windows_os)
{
	done_testing();
	exit;
}

# Get a slot terminated while the walsender is active
# We do this by sending SIGSTOP to the walsender.  Skip this on Windows.
my $node_primary3 = PostgreSQL::Test::Cluster->new('primary3');
$node_primary3->init(allows_streaming => 1, extra => ['--wal-segsize=1']);
$node_primary3->append_conf(
	'postgresql.conf', qq(
	min_wal_size = 2MB
	max_wal_size = 2MB
	log_checkpoints = yes
	max_slot_wal_keep_size = 1MB
	));
$node_primary3->start;
$node_primary3->safe_psql('postgres',
	"SELECT pg_create_physical_replication_slot('rep3')");
# Take backup
$backup_name = 'my_backup';
$node_primary3->backup($backup_name);
# Create standby
my $node_standby3 = PostgreSQL::Test::Cluster->new('standby_3');
$node_standby3->init_from_backup($node_primary3, $backup_name,
	has_streaming => 1);
$node_standby3->append_conf('postgresql.conf', "primary_slot_name = 'rep3'");
$node_standby3->start;
$node_primary3->wait_for_catchup($node_standby3);

my $senderpid;

# We've seen occasional cases where multiple walsender pids are still active
# at this point, apparently just due to process shutdown being slow. To avoid
# spurious failures, retry a couple times.
my $i = 0;
while (1)
{
	my ($stdout, $stderr);

	$senderpid = $node_primary3->safe_psql('postgres',
		"SELECT pid FROM pg_stat_activity WHERE backend_type = 'walsender'");

	last if $senderpid =~ qr/^[0-9]+$/;

	diag "multiple walsenders active in iteration $i";

	# show information about all active connections
	$node_primary3->psql(
		'postgres',
		"\\a\\t\nSELECT * FROM pg_stat_activity",
		stdout => \$stdout,
		stderr => \$stderr);
	diag $stdout, $stderr;

	if ($i++ == 10 * $PostgreSQL::Test::Utils::timeout_default)
	{
		# An immediate shutdown may hide evidence of a locking bug. If
		# retrying didn't resolve the issue, shut down in fast mode.
		$node_primary3->stop('fast');
		$node_standby3->stop('fast');
		die "could not determine walsender pid, can't continue";
	}

	usleep(100_000);
}

like($senderpid, qr/^[0-9]+$/, "have walsender pid $senderpid");

my $receiverpid = $node_standby3->safe_psql('postgres',
	"SELECT pid FROM pg_stat_activity WHERE backend_type = 'walreceiver'");
like($receiverpid, qr/^[0-9]+$/, "have walreceiver pid $receiverpid");

$logstart = -s $node_primary3->logfile;
# freeze walsender and walreceiver. Slot will still be active, but walreceiver
# won't get anything anymore.
kill 'STOP', $senderpid, $receiverpid;
$node_primary3->advance_wal(2);

my $msg_logged = 0;
my $max_attempts = $PostgreSQL::Test::Utils::timeout_default;
while ($max_attempts-- >= 0)
{
	if ($node_primary3->log_contains(
			"terminating process $senderpid to release replication slot \"rep3\"",
			$logstart))
	{
		$msg_logged = 1;
		last;
	}
	sleep 1;
}
ok($msg_logged, "walsender termination logged");

# Now let the walsender continue; slot should be killed now.
# (Must not let walreceiver run yet; otherwise the standby could start another
# one before the slot can be killed)
kill 'CONT', $senderpid;
$node_primary3->poll_query_until('postgres',
	"SELECT wal_status FROM pg_replication_slots WHERE slot_name = 'rep3'",
	"lost")
  or die "timed out waiting for slot to be lost";

$msg_logged = 0;
$max_attempts = $PostgreSQL::Test::Utils::timeout_default;
while ($max_attempts-- >= 0)
{
	if ($node_primary3->log_contains(
			'invalidating obsolete replication slot "rep3"', $logstart))
	{
		$msg_logged = 1;
		last;
	}
	sleep 1;
}
ok($msg_logged, "slot invalidation logged");

# Now let the walreceiver continue, so that the node can be stopped cleanly
kill 'CONT', $receiverpid;

$node_primary3->stop;
$node_standby3->stop;

# =============================================================================
# Testcase start: Check inactive_since property of the streaming standby's slot
#

# Initialize primary node
my $primary4 = PostgreSQL::Test::Cluster->new('primary4');
$primary4->init(allows_streaming => 'logical');
$primary4->start;

# Take backup
$backup_name = 'my_backup4';
$primary4->backup($backup_name);

# Create a standby linking to the primary using the replication slot
my $standby4 = PostgreSQL::Test::Cluster->new('standby4');
$standby4->init_from_backup($primary4, $backup_name, has_streaming => 1);

my $sb4_slot = 'sb4_slot';
$standby4->append_conf('postgresql.conf', "primary_slot_name = '$sb4_slot'");

my $slot_creation_time = $primary4->safe_psql(
	'postgres', qq[
    SELECT current_timestamp;
]);

$primary4->safe_psql(
	'postgres', qq[
    SELECT pg_create_physical_replication_slot(slot_name := '$sb4_slot');
]);

# Get inactive_since value after the slot's creation. Note that the slot is
# still inactive till it's used by the standby below.
my $inactive_since =
  $primary4->validate_slot_inactive_since($sb4_slot, $slot_creation_time);

$standby4->start;

# Wait until standby has replayed enough data
$primary4->wait_for_catchup($standby4);

# Now the slot is active so inactive_since value must be NULL
is( $primary4->safe_psql(
		'postgres',
		qq[SELECT inactive_since IS NULL FROM pg_replication_slots WHERE slot_name = '$sb4_slot';]
	),
	't',
	'last inactive time for an active physical slot is NULL');

# Stop the standby to check its inactive_since value is updated
$standby4->stop;

# Let's restart the primary so that the inactive_since is set upon loading the
# slot from the disk.
$primary4->restart;

is( $primary4->safe_psql(
		'postgres',
		qq[SELECT inactive_since > '$inactive_since'::timestamptz FROM pg_replication_slots WHERE slot_name = '$sb4_slot' AND inactive_since IS NOT NULL;]
	),
	't',
	'last inactive time for an inactive physical slot is updated correctly');

$standby4->stop;

# Testcase end: Check inactive_since property of the streaming standby's slot
# =============================================================================

# =============================================================================
# Testcase start: Check inactive_since property of the logical subscriber's slot
my $publisher4 = $primary4;

# Create subscriber node
my $subscriber4 = PostgreSQL::Test::Cluster->new('subscriber4');
$subscriber4->init;

# Setup logical replication
my $publisher4_connstr = $publisher4->connstr . ' dbname=postgres';
$publisher4->safe_psql('postgres', "CREATE PUBLICATION pub FOR ALL TABLES");

$slot_creation_time = $publisher4->safe_psql(
	'postgres', qq[
    SELECT current_timestamp;
]);

my $lsub4_slot = 'lsub4_slot';
$publisher4->safe_psql('postgres',
	"SELECT pg_create_logical_replication_slot(slot_name := '$lsub4_slot', plugin := 'pgoutput');"
);

# Get inactive_since value after the slot's creation. Note that the slot is
# still inactive till it's used by the subscriber below.
$inactive_since =
  $publisher4->validate_slot_inactive_since($lsub4_slot, $slot_creation_time);

$subscriber4->start;
$subscriber4->safe_psql('postgres',
	"CREATE SUBSCRIPTION sub CONNECTION '$publisher4_connstr' PUBLICATION pub WITH (slot_name = '$lsub4_slot', create_slot = false)"
);

# Wait until subscriber has caught up
$subscriber4->wait_for_subscription_sync($publisher4, 'sub');

# Now the slot is active so inactive_since value must be NULL
is( $publisher4->safe_psql(
		'postgres',
		qq[SELECT inactive_since IS NULL FROM pg_replication_slots WHERE slot_name = '$lsub4_slot';]
	),
	't',
	'last inactive time for an active logical slot is NULL');

# Stop the subscriber to check its inactive_since value is updated
$subscriber4->stop;

# Let's restart the publisher so that the inactive_since is set upon
# loading the slot from the disk.
$publisher4->restart;

is( $publisher4->safe_psql(
		'postgres',
		qq[SELECT inactive_since > '$inactive_since'::timestamptz FROM pg_replication_slots WHERE slot_name = '$lsub4_slot' AND inactive_since IS NOT NULL;]
	),
	't',
	'last inactive time for an inactive logical slot is updated correctly');

# Testcase end: Check inactive_since property of the logical subscriber's slot
# =============================================================================

$publisher4->stop;
$subscriber4->stop;

done_testing();
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
