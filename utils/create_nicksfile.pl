#!/usr/bin/perl
use strict;
use Getopt::Long;


my $conn                = undef;
my %readline_funcs      = ( 'licq' => \&import_readline_licq );
my %open_funcs          = ( 'licq' => \&import_open_licq );
my %close_funcs         = ( 'licq' => \&import_close_licq );
my $funcname            = undef;
my $dirname             = undef;
my $filename            = undef;
my $imported            = 0;
my $not_imported        = 0;
my $debug               = 0;

main();
exit(0);


sub main {
        my ($server,$port,$nick,$pass,$func,$dir,$file,$outfile);
        my $dirfile;
        if (($dirfile=pop @ARGV) =~ /^\-/ || !$dirfile) {
                tell_usage();
                exit(0);
        }
        if ($dirfile =~ m|/|) {
                $dirfile =~ m|^(.*)(/.+)$|;
                $dir=$1;
                $file=$2;
        } else {
                $dir=undef;
                $file = $dirfile;
        }
        GetOptions(
                'from=s',  => \$func,
                'of=s',  => \$outfile,
                'debug',  => \$debug
        );
        if (!import_start($func,$dir,$file,$outfile,$debug)) {
                tell_usage();
        }
}

sub tell_usage {
        print "Usage: create_nicksfile.pl [--from=FROM] [--of=OUTPUTFILE] [--debug] FILENAME\n";
        print "       FROM defines which application we import from.\n",
        print "       Note that currently the only valid value for FROM is licq.\n";
        print "       For further information, you might want to do perldoc create_nicksfile.pl\n";
}

sub import_start {
        $funcname          = (shift) || 'licq';
        $dirname        = shift;
        $filename       = shift;
        my $outfile        = shift || 'bitlbee.nicks';
        $debug          = shift;
        my ($alias,$protocol,$name,$found);
        open(OUT,'>'.$outfile) || die "unable to open $outfile";
        if (defined $open_funcs{$funcname}) {
                if (&{$open_funcs{$funcname}}($dirname,$filename)) {
                        do {
                                ($alias,$protocol,$name,$found)=&{$readline_funcs{$funcname}}();
                                print OUT "$alias $protocol $name\n" if $found;
                        } while ($found);
                } else {
                        import_err('Unable to open '.$filename);
                        return 0;
                }
        } else {
                import_err($funcname.' is no defined import function.');
                return 0;
        }
        close OUT;
        &{$close_funcs{$funcname}}();
        return 1;
}

sub import_err {
        my $msg=shift;
        print "\nError: $msg\n";
}

sub import_open_licq {
        my ($dir,$name)=@_;
        return open(IN,'<'.$dir.'/users.conf');
}
sub import_close_licq {
        close IN;
}
sub import_readline_licq {
        my ($uin,$alias);
        my $line;
GETLINE:
        $line=<IN>;
        if ($line) {
                while ($line && $line !~ /^User\d+/) {
                        $line=<IN>;
                }
                if ($line) {
                        if ($line =~ /^User\d+\s*=\s*(\d+)(\.Licq)?$/) { # getting UIN
                                $uin=$1;
                                open(ALIAS,'<'.$dirname.'/users/'.$uin.'.Licq') ||
                                open(ALIAS,'<'.$dirname.'/users/'.$uin.'.uin') || do {
                                        warn "unable to open userfile for $uin";
                                        return (undef,undef,0);
                                };
                                while (<ALIAS>) {
                                        if (/^Alias\s*=\s*(.*)$/) {
                                                $alias=$1;
                                                $alias =~ s/\s+/_/g;
                                                last;
                                        }
                                }
                                close ALIAS;
                                $imported++;
                                return ($uin,3,$alias,1);
                        } else {
                                warn('Unknown line format: '.$line);
                                $not_imported++;
                                goto GETLINE; #### grrrr, sometimes there are negative uins in licq files...
                        }
                } else {
                        return (undef,undef,0);
                }
        } else {
                return undef;
        }
}

__END__

=head1 NAME

create_nicksfile.pl - Create a valid bitlbee .nicks file

=head1 SYNOPSIS

create_nicksfile.pl [--from=FROM] [--of=OUTPUTFILE] [--debug] FILENAME

        FROM defines which application we import from.
        Note that currently the only valid value for FROM 
        is licq.

        If of is missing, we write to bitlbee.nicks.

=head1 DESCRIPTION

We run thru the
files where the contacts reside and create
a bitlbee .nicks-file from them.

=head1 DEPENDENCIES

On the perlside, we need Getopt::Long.

=head1 CAVEATS

=head1 TODO

&import_readline_... should take a filehandle as argument.

Add more import functions. If you are interested,
to do so, you need to write the following functions:

=over

=item *

import_open_<WHATEVER>(DIR,FILENAME)

=item *

import_close_<WHATEVER>()

=item *

import_readline_<WHATEVER>()

=back

and add them to the hashes

=over

=item *

%readline_funcs

=item *

%open_funcs

=item *

%close_funcs

=back

at the top of this script.


=head1 AUTHORS

Christian Friedl <vijeno@chello.at>

Updated for the new Licq list firmat by Hugo Buddelmeijer <kmail@hugo.doemaarwat.nl>

=cut
