
# Copyright (c) 2021-2024, PostgreSQL Global Development Group

use strict;
use warnings FATAL => 'all';

use FindBin;
use lib "$FindBin::RealBin/..";

use File::Copy;
use File::Basename;
use LdapServer;
use PostgreSQL::Test::Utils;
use PostgreSQL::Test::Cluster;
use Test::More;

<<<<<<< HEAD
if ($ENV{with_ldap} eq 'yes')
{
	plan tests => 22;
}
else
=======
if ($ENV{with_ldap} ne 'yes')
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
{
	plan skip_all => 'LDAP not supported by this build';
}
elsif (!$ENV{PG_TEST_EXTRA} || $ENV{PG_TEST_EXTRA} !~ /\bldap\b/)
{
	plan skip_all =>
	  'Potentially unsafe test LDAP not enabled in PG_TEST_EXTRA';
}
elsif (!$LdapServer::setup)
{
	plan skip_all => $LdapServer::setup_error;
}

note "setting up LDAP server";

my $ldap_rootpw = 'secret';
my $ldap = LdapServer->new($ldap_rootpw, 'anonymous');    # use anonymous auth
$ldap->ldapadd_file('authdata.ldif');
$ldap->ldapsetpw('uid=test1,dc=example,dc=net', 'secret1');
$ldap->ldapsetpw('uid=test2,dc=example,dc=net', 'secret2');

<<<<<<< HEAD
my $ldap_datadir  = "${TestLib::tmp_check}/openldap-data";
my $slapd_certs   = "${TestLib::tmp_check}/slapd-certs";
my $slapd_conf    = "${TestLib::tmp_check}/slapd.conf";
my $slapd_pidfile = "${TestLib::tmp_check}/slapd.pid";
my $slapd_logfile = "${TestLib::log_path}/slapd.log";
my $ldap_conf     = "${TestLib::tmp_check}/ldap.conf";
my $ldap_server   = 'localhost';
my $ldap_port     = get_free_port();
my $ldaps_port    = get_free_port();
my $ldap_url      = "ldap://$ldap_server:$ldap_port";
my $ldaps_url     = "ldaps://$ldap_server:$ldaps_port";
my $ldap_basedn   = 'dc=example,dc=net';
my $ldap_rootdn   = 'cn=Manager,dc=example,dc=net';
my $ldap_rootpw   = 'secret';
my $ldap_pwfile   = "${TestLib::tmp_check}/ldappassword";

note "setting up slapd";

append_to_file(
	$slapd_conf,
	qq{include $ldap_schema_dir/core.schema
include $ldap_schema_dir/cosine.schema
include $ldap_schema_dir/nis.schema
include $ldap_schema_dir/inetorgperson.schema

pidfile $slapd_pidfile
logfile $slapd_logfile

access to *
        by * read
        by anonymous auth

database ldif
directory $ldap_datadir

TLSCACertificateFile $slapd_certs/ca.crt
TLSCertificateFile $slapd_certs/server.crt
TLSCertificateKeyFile $slapd_certs/server.key

suffix "dc=example,dc=net"
rootdn "$ldap_rootdn"
rootpw $ldap_rootpw});

# don't bother to check the server's cert (though perhaps we should)
append_to_file(
	$ldap_conf,
	qq{TLS_REQCERT never
});

mkdir $ldap_datadir or die;
mkdir $slapd_certs  or die;

system_or_bail "openssl", "req", "-new", "-nodes", "-keyout",
  "$slapd_certs/ca.key", "-x509", "-out", "$slapd_certs/ca.crt", "-subj",
  "/CN=CA";
system_or_bail "openssl", "req", "-new", "-nodes", "-keyout",
  "$slapd_certs/server.key", "-out", "$slapd_certs/server.csr", "-subj",
  "/CN=server";
system_or_bail "openssl", "x509", "-req", "-in", "$slapd_certs/server.csr",
  "-CA", "$slapd_certs/ca.crt", "-CAkey", "$slapd_certs/ca.key",
  "-CAcreateserial", "-out", "$slapd_certs/server.crt";

system_or_bail $slapd, '-f', $slapd_conf, '-h', "$ldap_url $ldaps_url";

END
{
	kill 'INT', `cat $slapd_pidfile` if -f $slapd_pidfile;
}

append_to_file($ldap_pwfile, $ldap_rootpw);
chmod 0600, $ldap_pwfile or die;

# wait until slapd accepts requests
my $retries = 0;
while (1)
{
	last
	  if (
		system_log(
			"ldapsearch", "-h", $ldap_server, "-p",
			$ldap_port,   "-s", "base",       "-b",
			$ldap_basedn, "-D", $ldap_rootdn, "-y",
			$ldap_pwfile, "-n", "'objectclass=*'") == 0);
	die "cannot connect to slapd" if ++$retries >= 300;
	note "waiting for slapd to accept requests...";
	Time::HiRes::usleep(1000000);
}

$ENV{'LDAPURI'}    = $ldap_url;
$ENV{'LDAPBINDDN'} = $ldap_rootdn;
$ENV{'LDAPCONF'}   = $ldap_conf;

note "loading LDAP data";

system_or_bail 'ldapadd',    '-x', '-y', $ldap_pwfile, '-f', 'authdata.ldif';
system_or_bail 'ldappasswd', '-x', '-y', $ldap_pwfile, '-s', 'secret1',
  'uid=test1,dc=example,dc=net';
system_or_bail 'ldappasswd', '-x', '-y', $ldap_pwfile, '-s', 'secret2',
  'uid=test2,dc=example,dc=net';
=======
my ($ldap_server, $ldap_port, $ldaps_port, $ldap_url,
	$ldaps_url, $ldap_basedn, $ldap_rootdn
) = $ldap->prop(qw(server port s_port url s_url basedn rootdn));

# don't bother to check the server's cert (though perhaps we should)
$ENV{'LDAPTLS_REQCERT'} = "never";
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c

note "setting up PostgreSQL instance";

my $node = PostgreSQL::Test::Cluster->new('node');
$node->init;
$node->append_conf('postgresql.conf', "log_connections = on\n");
$node->start;

$node->safe_psql('postgres', 'CREATE USER test0;');
$node->safe_psql('postgres', 'CREATE USER test1;');
$node->safe_psql('postgres', 'CREATE USER "test2@example.net";');

note "running tests";

sub test_access
{
	local $Test::Builder::Level = $Test::Builder::Level + 1;

<<<<<<< HEAD
	my $res =
	  $node->psql('postgres', undef,
				  extra_params => [ '-U', $role, '-c', 'SELECT 1' ]);
	is($res, $expected_res, $test_name);
	return;
=======
	my ($node, $role, $expected_res, $test_name, %params) = @_;
	my $connstr = "user=$role";

	if ($expected_res eq 0)
	{
		$node->connect_ok($connstr, $test_name, %params);
	}
	else
	{
		# No checks of the error message, only the status code.
		$node->connect_fails($connstr, $test_name, %params);
	}
>>>>>>> c1ff2d8bc5be55e302731a16aaff563b7f03ed7c
}

note "simple bind";

unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapserver=$ldap_server ldapport=$ldap_port ldapprefix="uid=" ldapsuffix=",dc=example,dc=net"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'wrong';
test_access(
	$node, 'test0', 2,
	'simple bind authentication fails if user not found in LDAP',
	log_unlike => [qr/connection authenticated:/]);
test_access(
	$node, 'test1', 2,
	'simple bind authentication fails with wrong password',
	log_unlike => [qr/connection authenticated:/]);

$ENV{"PGPASSWORD"} = 'secret1';
test_access(
	$node, 'test1', 0,
	'simple bind authentication succeeds',
	log_like => [
		qr/connection authenticated: identity="uid=test1,dc=example,dc=net" method=ldap/
	],);

# require_auth=password should complete successfully; other methods should fail.
$node->connect_ok("user=test1 require_auth=password",
	"password authentication required, works with ldap auth");
$node->connect_fails("user=test1 require_auth=scram-sha-256",
	"SCRAM authentication required, fails with ldap auth");

note "search+bind";

unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapserver=$ldap_server ldapport=$ldap_port ldapbasedn="$ldap_basedn"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'wrong';
test_access($node, 'test0', 2,
	'search+bind authentication fails if user not found in LDAP');
test_access($node, 'test1', 2,
	'search+bind authentication fails with wrong password');
$ENV{"PGPASSWORD"} = 'secret1';
test_access(
	$node, 'test1', 0,
	'search+bind authentication succeeds',
	log_like => [
		qr/connection authenticated: identity="uid=test1,dc=example,dc=net" method=ldap/
	],);

note "multiple servers";

unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapserver="$ldap_server $ldap_server" ldapport=$ldap_port ldapbasedn="$ldap_basedn"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'wrong';
test_access($node, 'test0', 2,
	'search+bind authentication fails if user not found in LDAP');
test_access($node, 'test1', 2,
	'search+bind authentication fails with wrong password');
$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0, 'search+bind authentication succeeds');

note "multiple servers";

unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapserver="$ldap_server $ldap_server" ldapport=$ldap_port ldapbasedn="$ldap_basedn"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'wrong';
test_access($node, 'test0', 2,
	'search+bind authentication fails if user not found in LDAP');
test_access($node, 'test1', 2,
	'search+bind authentication fails with wrong password');
$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0, 'search+bind authentication succeeds');

note "LDAP URLs";

unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapurl="$ldap_url" ldapprefix="uid=" ldapsuffix=",dc=example,dc=net"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'wrong';
test_access($node, 'test0', 2,
	'simple bind with LDAP URL authentication fails if user not found in LDAP'
);
test_access($node, 'test1', 2,
	'simple bind with LDAP URL authentication fails with wrong password');
$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0,
	'simple bind with LDAP URL authentication succeeds');

unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapurl="$ldap_url/$ldap_basedn?uid?sub"});
$node->restart;

$ENV{"PGPASSWORD"} = 'wrong';
test_access($node, 'test0', 2,
	'search+bind with LDAP URL authentication fails if user not found in LDAP'
);
test_access($node, 'test1', 2,
	'search+bind with LDAP URL authentication fails with wrong password');
$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0,
	'search+bind with LDAP URL authentication succeeds');

note "search filters";

unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapserver=$ldap_server ldapport=$ldap_port ldapbasedn="$ldap_basedn" ldapsearchfilter="(|(uid=\$username)(mail=\$username))"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'secret1';
test_access(
	$node, 'test1', 0,
	'search filter finds by uid',
	log_like => [
		qr/connection authenticated: identity="uid=test1,dc=example,dc=net" method=ldap/
	],);
$ENV{"PGPASSWORD"} = 'secret2';
test_access(
	$node,
	'test2@example.net',
	0,
	'search filter finds by mail',
	log_like => [
		qr/connection authenticated: identity="uid=test2,dc=example,dc=net" method=ldap/
	],);

note "search filters in LDAP URLs";

unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapurl="$ldap_url/$ldap_basedn??sub?(|(uid=\$username)(mail=\$username))"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0, 'search filter finds by uid');
$ENV{"PGPASSWORD"} = 'secret2';
test_access($node, 'test2@example.net', 0, 'search filter finds by mail');

# This is not documented: You can combine ldapurl and other ldap*
# settings.  ldapurl is always parsed first, then the other settings
# override.  It might be useful in a case like this.
unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapurl="$ldap_url/$ldap_basedn??sub" ldapsearchfilter="(|(uid=\$username)(mail=\$username))"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0, 'combined LDAP URL and search filter');

note "diagnostic message";

# note bad ldapprefix with a question mark that triggers a diagnostic message
unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapserver=$ldap_server ldapport=$ldap_port ldapprefix="?uid=" ldapsuffix=""}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 2, 'any attempt fails due to bad search pattern');

note "TLS";

# request StartTLS with ldaptls=1
unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapserver=$ldap_server ldapport=$ldap_port ldapbasedn="$ldap_basedn" ldapsearchfilter="(uid=\$username)" ldaptls=1}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0, 'StartTLS');

# request LDAPS with ldapscheme=ldaps
unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapserver=$ldap_server ldapscheme=ldaps ldapport=$ldaps_port ldapbasedn="$ldap_basedn" ldapsearchfilter="(uid=\$username)"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0, 'LDAPS');

# request LDAPS with ldapurl=ldaps://...
unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapurl="$ldaps_url/$ldap_basedn??sub?(uid=\$username)"}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 0, 'LDAPS with URL');

# bad combination of LDAPS and StartTLS
unlink($node->data_dir . '/pg_hba.conf');
$node->append_conf('pg_hba.conf',
	qq{local all all ldap ldapurl="$ldaps_url/$ldap_basedn??sub?(uid=\$username)" ldaptls=1}
);
$node->restart;

$ENV{"PGPASSWORD"} = 'secret1';
test_access($node, 'test1', 2, 'bad combination of LDAPS and StartTLS');

done_testing();
