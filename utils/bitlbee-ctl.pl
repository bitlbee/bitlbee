#!/usr/bin/perl
# Simple front-end to BitlBee's administration commands
# Copyright (C) 2006 Jelmer Vernooij <jelmer@samba.org>

use IO::Socket;
use Getopt::Long;
use strict;
use warnings;

my $opt_help;
my $opt_socketfile = "/var/run/bitlbee";

sub ShowHelp 
{
	print 
"bitlbee-ctl.pl [options] command ...

Available options:

  --ipc-socket=SOCKET	Override path to IPC socket [$opt_socketfile]
  --help				Show this help message

Available commands:
	
	die

";
	exit (0);
}

GetOptions (
	    'help|h|?' => \&ShowHelp,
		'ipc-socket=s' => \$opt_socketfile
	    ) or exit(1);

my $client = IO::Socket::UNIX->new(Peer => $opt_socketfile,
								Type => SOCK_STREAM,
								Timeout => 10);
								
if (not $client) {
	print "Error connecting to $opt_socketfile: $@\n";
	exit(1);
}

my $cmd = shift @ARGV;

if (not defined($cmd)) {
	print "Usage: bitlbee-ctl.pl [options] command ...\n";
	exit(1);
}

if ($cmd eq "die") {
	$client->send("DIE\r\n");
} else {
	print "No such command: $cmd\n";
	exit(1);
}

$client->close();
