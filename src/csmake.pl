#!/usr/bin/env perl
#
# (C) Copyright 2006-2026 Diomidis Spinellis
#
# This file is part of CScout.
#
# CScout is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# CScout is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with CScout.  If not, see <http://www.gnu.org/licenses/>.
#
# Run make with gcc, cc, ld, ar, mv replaced with spying versions
# included in this file.
# Also, when invoked as cscc, run cc replaced with its spying version.
# Create a CScout-compatible make.cs file
#
#

$debug = 0;

use Cwd 'abs_path';
use Getopt::Std;

# Used for reading the source of the other spy programs
$script_name =$0;

my $tmpdir = ($ENV{TMP} ? $ENV{TMP} : "/tmp");

# Find and set the installation directory
if (-d '.cscout') {
	$instdir = '.cscout';
} elsif (defined($ENV{CSCOUT_HOME})) {
	$instdir = $ENV{CSCOUT_HOME};
} elsif (defined($ENV{HOME}) && -d $ENV{HOME} . '/.cscout') {
	$instdir = $ENV{HOME} . '/.cscout';
} elsif (-d 'INSTALL_INCLUDE') {
	$instdir = 'INSTALL_INCLUDE';
} else {
	print STDERR "Unable to identify a CScout installation directory\n";
	print STDERR 'Create ./.cscout, or $HOME/.cscout, or set the $CSCOUT_HOME variable' . "\n";
	exit(1);
}

if (!-r ($f = "$instdir/csmake-pre-defs.h")) {
	print STDERR "Unable to read $f: $!\n";
	print STDERR "Create the file in the directory $instdir\nby copying the appropriate compiler-specific file\n";
	exit(1);
}
$instdir = abs_path($instdir);

# Copy arguments into TEMP_ARGV to use them with real make
my @TEMP_ARGV = @ARGV;
@ARGV = ();
# Parse CSMAKEFLAGS
if ($ENV{'CSMAKEFLAGS'}) {
	@ARGV = split(/\s+/, $ENV{'CSMAKEFLAGS'});
}
my %options=();  # csmake options
my $csmake_opts = "Ad:hkN:o:p:s:T:t:";
# Parse options from CSMAKEFLAGS
getopts($csmake_opts, \%options);

my ($index) = grep { $TEMP_ARGV[$_] eq '--' } (0 .. @TEMP_ARGV-1);
my @MAKEARGS;

if (defined $index) {
    # Parse command line arguments and separate them into csmake arguments and make arguments.
    foreach my $i (0 .. $#TEMP_ARGV) {
        if ($i < $index) {
            push(@ARGV, $TEMP_ARGV[$i]);
        } elsif ($i > $index) {
            push(@MAKEARGS, $TEMP_ARGV[$i]);
        }
    }
    getopts($csmake_opts, \%options);
} else {
    @MAKEARGS = @TEMP_ARGV;
}

if (defined $options{d}) {
	$debug = $options{d};
}

if (defined $options{h}) {
print <<HELP;
usage: csmake [ [-A] [-d] [-k] [-o output] [-s cs_files_directory] [-T temporary_directory ] [-t time_file] [-N rules_file] [-h] -- ] [make(1) options]
    -d N              Run in debug mode (it also keeps spy directory in place).
    -h                Print help message.
    -k                Keep temporary directory in place.
    -o file           Specify output file; (default make.cs, - for stdout)
    -p prefix         Cross-compilation tool prefix, e.g. aarch64-linux-gnu-
    -s cs_files_dir   Create a separate CScout .cs file for each executable.
    -A                Generate cs projects for static libraries.
    -N rules_file     Run on an existing rules file.
    -T temporary_dir  Set temporary directory.
    -t time_file      Set file to save command timing information.
HELP
exit();
}

my $prefix = $options{p} // '';

if ($0 =~ m/\bcscc$/) {
	# Run as a C compiler invocation
	prepare_spy_environment($options{T});
	spy($prefix, 'gcc', 'spy-gcc');
	$exit_status = system(("${prefix}gcc", @MAKEARGS));
	check_exit("${prefix}gcc", $exit_status);
	push(@toclean, 'rules');
	open(IN, "$ENV{CSCOUT_SPY_TMPDIR}/rules") || die "Unable to open $ENV{CSCOUT_SPY_TMPDIR}/rules for reading: $!\nMake sure you have specified appropriate compiler options.\n";
} elsif (defined $options{N}) {
	# Run on an existing rules file
	open(IN, $options{N}) || die "Unable to open $options{N} for reading: $!\n";
} else {
	prepare_spy_environment($options{T});
	spy($prefix, 'gcc', 'spy-gcc');
	spy($prefix, 'cc', 'spy-gcc');
	spy($prefix, 'clang', 'spy-gcc');
	spy('', 'x86_64-linux-gnu-gcc', 'spy-gcc');
	spy('', $ENV{'CC'}, 'spy-gcc') if defined($ENV{'CC'});
	spy($prefix, 'ld', 'spy-ld');
	spy($prefix, 'ar', 'spy-ar');
	spy('', 'mv', 'spy-mv');
	spy('', 'install', 'spy-install');
	my $exit_status = system(("make", @MAKEARGS));
	check_exit('make', $exit_status);
	push(@toclean, 'rules');
	if (!open(IN, "$ENV{CSCOUT_SPY_TMPDIR}/rules")) {
		print STDERR "Warning: Unable to open $ENV{CSCOUT_SPY_TMPDIR}/rules for reading: $!\nMake sure a make command that creates an executable will precede or follow\nthis run.\n";
		exit 0;
	}
}

# Create a CScout .cs file
my $output_file = $options{o} // 'make.cs';
if ($output_file eq '-') {
	*OUT = *STDOUT;  # Alias STDOUT to OUT
} else {
	open(OUT, '>', $output_file) || die "Unable to open $output_file for writing: $!\n";
}
# Create a Directory to save CScout .cs files for each project
my $directory = "";
if (defined $options{s}) {
	$directory = $options{s};
	if (! -e $directory) {
		unless(mkdir $directory) {
			die "Unable to create $directory\n";
		}
	}
}
# Find all install rules and save them to a hash
my %irules;
while (<IN>) {
	chop;
	if (/^INSTALL /) {
		($dummy, $excecutable, $path) = split;
		if (exists $irules{$excecutable}) {
			push @{ $irules{$excecutable} }, $path;
		} else {
			$irules{$excecutable} = [ $path ];
		}
		next;
    }
}

my $time_file;
if (defined $options{t}) {
	open($time_file, '>', $options{t}) || die "Unable to open timing file $options{t}: $!\n";
}


seek (IN, 0, 0);
# Read a spy-generated rules file
while (<IN>) {
	chop;
	if (/^RENAME /) {
		($dummy, $old, $new) = split;
		$old{$new} =$old;
		next;
	} elsif (/^COMMENT (.*)/ && defined $options{t}) {
		print $time_file "$1\n";
	} elsif (/^BEGIN COMPILE/) {
		$state = 'COMPILE';
		undef @rules;
		undef $src;
		undef $process;
		undef $obj;
		next;
	} elsif (/^BEGIN LINK/) {
		$state = 'LINK';
		undef $exe;
		undef @obj;
		next;
	} elsif (/^BEGIN AR/) {
		$state = 'AR';
		undef $lib;
		undef @obj;
		next;
    } elsif (/^CMDLINE (.*)/) {
        $cmdline = $1;
    }
	if ($state eq 'COMPILE') {
		if (/^END COMPILE/) {
			die "Missing  source in rules file" unless defined ($src);
			die "Missing object in rules file" unless defined ($obj);
			# Allow for multiple rules for the same object
			$cd = $src;
			$cd =~ s,/[^/]+$,,;
			$rules{$obj} .= qq{
#pragma echo "Processing $src\\n"
#pragma block_enter
#pragma clear_defines
#pragma clear_include
#pragma pushd "$cd"
#include "$instdir/csmake-pre-defs.h"
} . join("\n", @rules) . qq{
#include "$instdir/csmake-post-defs.h"
$process
#pragma popd
#pragma block_exit
#pragma echo "Done processing $src\\n"
};
			undef $state;
		} elsif (/^INSRC (.*)/) {
			$src = $1;
			$process .= qq{#pragma process "$src"\n};
		} elsif (/^INMACRO (.*)/) {
			$process .= qq{#include "$1"\n};
		} elsif (/^OUTOBJ (.*)/) {
			$obj = $1;
		} elsif (/^CMDLINE/) {
			;
		} else {
			push(@rules, $_);
		}
	} elsif ($state eq 'LINK') {
		if (/^END LINK/) {
			die "Missing object in rules file" unless defined ($exe);
			if ($exe =~ m/\.(o|a|so|opic)$/) {
				# Output is a library or combined object file; just remember the rules
				undef $rule;
				for $o (@obj) {
					$o = ancestor($o);
					print STDERR "Warning: No compilation rule for $o\n" unless defined ($rules{$o});
					$rule .= $rules{$o};
				}
				$rules{$exe} = $rule;
				if ($exe =~ m/\.so$/) {
					create_project($exe);
				}
			} else {
				# Output is a real executable; start a project
				create_project($exe);
			}
			undef $state;
		} elsif (/^CMDLINE/) {
			;
		} elsif (/^OUTEXE (.*)/) {
			$exe = $1;
		} elsif (/^INOBJ (.*)/ || /^INLIB (.*)/) {
			push(@obj, $1);
		}
	} elsif ($state eq 'AR') {
		if (/^END AR/) {
			die "Missing library in rules file" unless defined ($lib);
			# Output is a library; just remember the rules.
			undef $rule;
			for $o (@obj) {
				$o = ancestor($o);
				print STDERR "Warning: No compilation rule for $o\n" unless defined ($rules{$o});
				$rule .= $rules{$o};
			}
			$rules{$lib} = $rule;
			if (defined $options{A}) {
				create_project($lib);
			}
			undef $state;
		} elsif (/^CMDLINE/) {
			;
		} elsif (/^OUTAR (.*)/) {
			$lib = $1;
		} elsif (/^INOBJ (.*)/) {
			push(@obj, $1);
		}
	}
}

if ($debug || defined $options{k}) {
	print "Leaving temporary files in $ENV{CSCOUT_SPY_TMPDIR}/$fname\n";
} else {
	# Clean temporary files
	for $fname (@toclean) {
		unlink("$ENV{CSCOUT_SPY_TMPDIR}/$fname");
	}
	rmdir($ENV{CSCOUT_SPY_TMPDIR});
}

exit 0;

# Print to many filehandles
sub
print_to_many
{
	my @args = @_;
	my $output = pop @args;
	foreach (@args) {
		print $_ $output;
	}
}


# Canonicalize filename
# Replace '/' or '\' with '#'
sub
can_filename
{
	my ($filename) = @_;
	$filename =~ tr|\\\/|##|;
	return ($filename);
}

# Prepare an environment for spying on command invocations
sub
prepare_spy_environment
{
	my ($option_T) = @_;
	if (defined $option_T) {
		if (! -d $option_T) {
			die "$option_T directory doesn't exist\n";
		}
		$ENV{CSCOUT_SPY_TMPDIR} = $option_T;
	} else {
		$ENV{CSCOUT_SPY_TMPDIR} = $tmpdir . "/spy-make.$$";
		mkdir($ENV{CSCOUT_SPY_TMPDIR}) || die "Unable to mkdir $ENV{CSCOUT_SPY_TMPDIR}: $!\n";
	}

	push(@toclean, 'empty.c');
	open(OUT, ">$ENV{CSCOUT_SPY_TMPDIR}/empty.c") || die "Unable to open $ENV{CSCOUT_SPY_TMPDIR}/empty.c for writing: $!\n";
	close(OUT);
	$ENV{PATH} = "$ENV{CSCOUT_SPY_TMPDIR}:$ENV{PATH}";
}

# Setup the environment to call spyProgName, instead of realProgName
# realProgName should be in the path
# spyProgName should be listed in this file in a BEGIN/END block
sub spy
{
	my($prefix, $realProgName, $spyProgName) = @_;
	# Read commands from this script
	open(IN, $script_name) || die "Unable to open $script_name for reading: $!\n";
	push(@toclean, "$prefix$realProgName");
	open(OUT, ">$ENV{CSCOUT_SPY_TMPDIR}/$prefix$realProgName") || die "Unable to open $ENV{CSCOUT_SPY_TMPDIR}/$prefix$realProgName for writing: $!\n";
	print OUT '#!/usr/bin/env perl
#
# Automatically-generated file
#
#

my $rulesfile = "$ENV{CSCOUT_SPY_TMPDIR}/rules";

# Use syswrite to avoid (for POSIX-compliant filesystems) garbled output
# from concurrent spy executions
open(RULES, ">>", $rulesfile) || die "Unable to open $rulesfile: $!\n";

# String to accumulate rules, so that they can be written with an atomic write
my $rules;

';
	print OUT "\$debug = $debug;\n";
	while (<IN>) {
		print OUT if (/^\#\@BEGIN $spyProgName/../^\#\@END/);
		print OUT if (/^\#\@BEGIN COMMON/../^\#\@END/);
	}
	close IN;
	close OUT;
	chmod(0755, "$ENV{CSCOUT_SPY_TMPDIR}/$prefix$realProgName");
}

# Return a file's original ancestor following a chain of renames
sub ancestor
{
	my($name) = @_;

	my($n) = $name;
	while (defined($old{$name})) {
		$name = $old{$name};
	}
	return ($name);
}

# Generate a CScout project.
sub
create_project
{
    my($name) = @_;
    my $install_paths = "";
    if (exists $irules{$name}) {
        @paths = @{ $irules{$name} };
        foreach (@paths) {
            $install_paths .= "\n#pragma install \"$_\"";
        }
    }
    my @new_obj = ();
    my @static_libs = ();
    for $o (@obj) {
        $o = ancestor($o);
        if ($o=~ m/\.a$/ and not grep( /$o/, @static_libs )) {
            push @static_libs, $o;
        }
        if (defined ($rules{$o})) {
            push @new_obj, $o;
        }
    }
    if (@new_obj) {
        # Create a CScout .cs file for current project
        my $filename = can_filename($name);
        if (defined $options{s}) {
            open(PROJ_OUT, ">$directory/$filename.cs") || die "Unable to open $directory/$filename.cs for writing: $!\n";
        }
        my $pragma_project_begin = qq{
#pragma echo "Processing project $name\\n"
#pragma echo "CMD $cmdline\\n"
#pragma echo "LIBRARIES @static_libs\\n"
#pragma project "$name"
#pragma block_enter$install_paths
};
        defined $options{s} ? print_to_many(OUT, PROJ_OUT, $pragma_project_begin) : print OUT $pragma_project_begin;
        for $o (@new_obj) {
            $o = ancestor($o);
            print STDERR "Warning: No compilation rule for $o\n" unless defined ($rules{$o});
            defined $options{s} ? print_to_many(OUT, PROJ_OUT, $rules{$o}) : print OUT $rules{$o};
        }
        my $pragma_project_end = qq{
#pragma block_exit
#pragma echo "Done processing project $name\\n\\n"
};
        defined $options{s} ? print_to_many(OUT, PROJ_OUT, $pragma_project_end) : print OUT $pragma_project_end;
        if (defined $options{s}) {
            close PROJ_OUT;
        }
    } else {
        print STDERR "Warning: No compilation rule for project $name\n";
        print STDERR "CMD: $cmdline\n" if ($debug);
    }
}


#@END

#@BEGIN spy-ar
#
# Spy on ar invocations and construct corresponding CScout directives
#

$real = which($0);

$origline = "ar " . join(' ', @ARGV);
$origline =~ s/\n/ /g;

@ARGV2 = @ARGV;
$op = shift @ARGV2;

# r: insert, m: move, q: quick append
if ($op !~ m/[rmq]/) {
	print STDERR "Just run ($real @ARGV))\n" if ($debug);
	$exit = system(($real, @ARGV)) / 256;
	print STDERR "Just run done ($exit)\n" if ($debug);
	exit $exit;
}

# Remove relative placement archive member name, if specified
shift @ARGV2 if ($op =~ m/[abi]/);

# Remove count
shift @ARGV2 if ($op =~ m/N/);

$archive = shift @ARGV2;

@ofiles = @ARGV2;

# Move into an existing archive, so effectively input it.
push(@ofiles, $archive) if ($op =~ m/m/);

$rules .= "BEGIN AR\n";
$rules .= "CMDLINE $origline\n";
$rules .= 'OUTAR ' . robust_abs_path($archive) . "\n";

# May be empty e.g. for archives of non-configured subsystems.
# Make rules empty rather than undefined to avoid spurious warnings.
for $ofile (@ofiles) {
	$rules .= "INOBJ " . abs_path($ofile) . "\n";
}
$rules .= "END AR\n";

syswrite(RULES, $rules);
close(RULES);

# Finally, execute the real ar
print STDERR "Finally run ($real @ARGV))\n" if ($debug);
exit system(($real, @ARGV)) / 256;

#@END

#@BEGIN spy-gcc
#
# Spy on gcc invocations and construct corresponding CScout directives
#
# Parsing the switches appears to be a tar pit (see incomplete version 1.1).
# Documentation is incomplete and inconsistent with the source code.
# Different gcc versions and installations add and remove options.
# Therefore it is easier to let gcc do the work
#

use File::Temp qw/ tempdir /;

$real = which($0);

# Gather input / output files and remove them from the command line
for ($i = 0; $i <= $#ARGV; $i++) {
	$arg = $ARGV[$i];
	if ($arg =~ m/\.c$/i) {
		push(@cfiles, $arg);
	} elsif ($arg =~ m/^-o(.+)$/ || $arg =~ m/^--output=(.*)/) {
		$output = $1;
		next;
	} elsif ($arg eq '-o' || $arg eq '--output') {
		$output = $ARGV[++$i];
		next;
	} elsif ($arg =~ m/^-L(.+)$/ || $arg =~ m/^--library-directory=(.*)/) {
		push(@ldirs, $1);
		next;
	} elsif ($arg eq '-L' || $arg eq '--library-directory') {
		push(@ldirs, $ARGV[++$i]);
		next;
	} elsif ($arg =~ m/^-l(.+)$/ || $arg =~ m/^--library=(.*)/) {
		push(@libs, $1);
		next;
	} elsif ($arg eq '-l' || $arg eq '--library') {
		push(@libs, $ARGV[++$i]);
		next;
	} elsif ($arg eq '-include' || $arg eq '--include') {
		push(@incfiles, 'INSRC ' . abs_if_exists($ARGV[++$i]));
		next;
	} elsif ($arg eq '-imacros' || $arg eq '--imacros') {
		push(@incfiles, 'INMACRO ' . abs_if_exists($ARGV[++$i]));
		next;
	} elsif ($arg =~ m/\.(o|obj|opic)$/i) {
		push(@ofiles, $arg);
		next;
	} elsif ($arg !~ m/\-fplugin\=/ && $arg =~ m/\.(a|so)$/i) {
		push(@afiles, $arg);
		next;
	} elsif ($arg =~ m/^\-M[TF]$/) {
		# Output or target of dependency; skip it for ARGV2
		$i++;
		next;
	} else {
		push(@ARGV2, $arg) unless ($arg =~ m/^\-M/);
	}
	# Write dependencies
	# Some builds do that while compiling, so we can't just bail out
	$depwrite = 1 if (
		($arg =~ m/^-M/ || $arg =~ m/-dependencies/) &&
		$arg !~ '-MD' && $arg !~ '-MMD' &&
		$arg !~ "--write-dependencies" &&
		$arg !~ "--write-user-dependencies");
	$bailout = 1 if ($arg eq '--preprocess' || $arg eq '-E');
	$bailout = 1 if ($arg eq '--assemble' || $arg eq '-S');
	$bailout = 1 if ($arg =~ m/-print-file-name/);
	$compile = 1 if ($arg eq '--compile' || $arg eq '-c');
}

$bailout = 1 if ($depwrite && !$compile);

# We don't handle assembly files or preprocessing
if ($bailout) {
	print STDERR "Just run ($real @ARGV))\n" if ($debug);
	$exit = system(($real, @ARGV)) / 256;
	print STDERR "Just run done ($exit)\n" if ($debug);
	exit $exit;
}

my @incs;
my @defs;

my $tmpdir = tempdir('gcc-out-XXXX', DIR => $ENV{CSCOUT_SPY_TMPDIR});

if ($#cfiles >= 0) {
	push(@ARGV2, $ENV{CSCOUT_SPY_TMPDIR} . '/empty.c');
	# Escape shell metacharacters
	for ($i = 0; $i < $#ARGV2; $i++) {
		$ARGV2[$i] =~ s/([][*\\"';|()<> \t\n{}&])/\\$1/g;
	}
	$cmdline = $real . ' ' . join(' ', @ARGV2);
	print STDERR "Get gcc incs/defs for C files @cfiles by running $cmdline\n" if ($debug);

	my $gcc_out = "$tmpdir/out.txt";
	print STDERR "Redirecting to $gcc_out\n" if ($debug > 2);


	# Gather include path
	print STDERR "Run $cmdline -v -E 2>&1 >$gcc_out\n" if ($debug > 2);
	my $exit_status = system("$cmdline -v -E >$gcc_out 2>&1");
	check_exit("$cmdline -v -E", $exit_status, $gcc_out);
	open(IN, '<', $gcc_out) || die "Unable to read $cmdline: $!\n";
	my $gather;
	print STDERR "Parse -v -E output\n" if ($debug > 2);
	while (<IN>) {
		chop;
		$gather = 1 if (/\#include \"\.\.\.\" search starts here\:/);
		next if (/ search starts here\:/);
		last if (/End of search list\./);
		if ($gather) {
			s/^\s*//;
			push(@incs, '#pragma includepath "' . abs_path($_) . '"');
			print STDERR "path=[$_]\n" if ($debug > 2);
		}
	}
	close(IN);

	# Gather macro definitions
	$exit_status = system("$cmdline -dD -E >$gcc_out 2>&1");
	check_exit("$cmdline -dD -E", $exit_status, $gcc_out);
	open(IN, '<', $gcc_out) || die "Unable to read $gcc_out $!\n";
	print STDERR "Parse -dD output\n" if ($debug > 2);
	while (<IN>) {
		chop;
		# Skip line directives, e.g.: # 525 "types.h"
		next if (/^\s*\#\s*\d/);
		push(@defs, $_);
		print STDERR "debug=[$_]\n" if ($debug > 2);
	}
	close(IN);

	unlink($gcc_out);

	print STDERR "Parsed incs=[@incs] defs=[@defs]\n" if ($debug > 2);
}

$origline = "gcc " . join(' ', @ARGV);
$origline =~ s/\n/ /g;

print STDERR "Create compilation rules\n" if ($debug > 1);
# Output compilation rules
for $cfile (@cfiles) {
	$rules .= "BEGIN COMPILE\n";
	$rules .= "CMDLINE $origline\n";
	for $if (@incfiles) {
		$rules .= "$if\n";
	}
	$rules .= "INSRC " . abs_path($cfile) . "\n";
	if ($compile) {
		if ($output) {	# cc -c -o foo.o foo.c
			$rules .= "OUTOBJ " . robust_abs_path($output) . "\n";
		} else {	# cc -c foo.c
			my $coutput= $cfile;
			$coutput =~ s/\.c$/.o/i;
			$coutput =~ s,.*/,,;
			$rules .= "OUTOBJ " . robust_abs_path($coutput) . "\n";
		}
	} else {		# cc -o foo foo.c OR cc foo.c
		# Implicit output file
		# Obtain a globally unique identifier across all csmake spy
		# program runs by using the current rules file offset. This
		# will be always increasing across all programs.
		my $ofile = "/$tmpdir/csmake-ofile-" . tell(RULES) . '.o';
		print STDERR "Add implicit output file $ofile\n" if ($debug > 1);
		push(@implicit_ofiles, $ofile);
		$rules .= "OUTOBJ $ofile\n";
	}
	print STDERR "Adding to rules incs=[@incs] defs=[@defs]\n" if ($debug > 2);
	$rules .= join("\n", @incs) . "\n";
	$rules .= join("\n", @defs) . "\n";
	$rules .= "END COMPILE\n";
}

if (!$compile && !$depwrite && ($#ofiles >= 0 || $#implicit_ofiles >= 0 || $#afiles >= 0)) {

	$rules .= "BEGIN LINK\n";
	$rules .= "CMDLINE $origline\n";
	if ($output) {
		$rules .= "OUTEXE $output\n";
	} else {
		$rules .= "OUTEXE a.out\n";
	}
	for $ofile (@ofiles) {
		$rules .= "INOBJ " . abs_path($ofile) . "\n";
	}
	for $ofile (@implicit_ofiles) {
		$rules .= "INOBJ $ofile\n";
	}
	for $afile (@afiles) {
		$rules .= "INLIB " . abs_path($afile) . "\n";
	}
	for $libfile (@libs) {
		for $ldir (@ldirs) {
			if (-r ($try = "$ldir/lib$libfile.a")) {
				$rules .= "INLIB " . abs_path($try) . "\n";
				last;
			}
		}
	}
	$rules .= "END LINK\n";
}

# Finally, execute the real gcc keeping time information
my $time_out = "$tmpdir/out.txt";
$rules .= "COMMENT COMPILE $real " . join(' ', @ARGV) . "\n";

# Determina appropriate time flags
my $time_gnu = `/usr/bin/time --version 2>&1`;
my @time_flags;
if ($time_gnu =~ /GNU/) {
	@time_flags = ('-f', '%D %e %F %I %K %M %O %S %U')
} else {
	@time_flags = ('-l')
}

unshift(@ARGV, @time_flags, '-o', $time_out, $real);
print STDERR "Finally run (/usr/bin/time @ARGV))\n" if ($debug);
my $exit_code = system(('/usr/bin/time', @ARGV)) / 256;
open(IN, '<', $time_out) || die "Unable to read time output: $!\n";
while (my $time = <IN>) {
	$rules .= "COMMENT TIME $time";
}
close(IN);
unlink($time_out);
rmdir($tmpdir);

syswrite(RULES, $rules);
close(RULES);

exit $exit_code;

# Return the absolute file name of a file, if the file exists in the
# current directory
sub abs_if_exists
{
	my($fname) = @_;
	return (-r $fname) ? abs_path($fname) : $fname;
}

#@END

#@BEGIN spy-ld
#
# Spy on ld invocations and construct corresponding CScout directives
#

$real = which($0);

# Gather input / output files and remove them from the command line
for ($i = 0; $i <= $#ARGV; $i++) {
	$arg = $ARGV[$i];
	if ($arg =~ m/\.(o|lo)$/i) {
		push(@ofiles, $arg);
	} elsif ($arg =~ m/^-o(.+)$/ || $arg =~ m/^--output=(.*)/) {
		$output = $1;
		next;
	} elsif ($arg eq '-o' || $arg eq '--output') {
		$output = $ARGV[++$i];
		next;
	} elsif ($arg =~ m/^-L(.+)$/ || $arg =~ m/^--library-path=(.*)/) {
		push(@ldirs, $1);
		next;
	} elsif ($arg eq '-L' || $arg eq '--library-path') {
		push(@ldirs, $ARGV[++$i]);
		next;
	} elsif ($arg =~ m/^-l(.+)$/ || $arg =~ m/^--library=(.*)/) {
		push(@libs, $1);
		next;
	} elsif ($arg eq '-l' || $arg eq '--library') {
		push(@libs, $ARGV[++$i]);
		next;
	} elsif ($arg =~ m/\.(a|so)$/i) {
		push(@afiles, $arg);
		next;
	} else {
		push(@ARGV2, $arg);
	}
}

$origline = "ld " . join(' ', @ARGV);
$origline =~ s/\n/ /g;


if ($#ofiles >= 0 || $#afiles >= 0) {
	$rules .= "BEGIN LINK\n";
	$rules .= "CMDLINE $origline\n";
	if ($output) {
		$rules .= 'OUTEXE ' . robust_abs_path($output) . "\n";
	} else {
		$rules .= 'OUTEXE ' . robust_abs_path('a.out') . "\n";
	}
	for $ofile (@ofiles) {
		$rules .= 'INOBJ ' . abs_path($ofile) . "\n";
	}
	for $afile (@afiles) {
		$rules .= 'INLIB ' . abs_path($afile) . "\n";
	}
	for $libfile (@libs) {
		for $ldir (@ldirs) {
			if (-r ($try = "$ldir/lib$libfile.a")) {
				$rules .= "INLIB " . abs_path($try) . "\n";
				last;
			}
		}
	}
	$rules .= "END LINK\n";
}

syswrite(RULES, $rules);
close(RULES);

# Finally, execute the real ld
print STDERR "Finally run ($real @ARGV))\n" if ($debug);
exit system(($real, @ARGV)) / 256;

#@END

#@BEGIN spy-mv
#
# Spy on ar invocations and construct corresponding CScout directives
#

$real = which($0);

$origline = "mv " . join(' ', @ARGV);
$origline =~ s/\n/ /g;

@ARGV2 = @ARGV;

# Remove options
while ($ARGV2[0] =~ m/^-/) {
	shift @ARGV2;
}

$rules .= "RENAMELINE $origline\n";
if ($#ARGV2 == 1) {
	$rules .= 'RENAME ' . abs_path($ARGV2[0]) . ' ' . robust_abs_path($ARGV2[1]) . "\n";
} else {
	$dir = pop(@ARGV2);
	for $f (@ARGV2) {
		$rules .= 'RENAME ' . abs_path($f) . ' ' . robust_abs_path("$dir/$f") . "\n";
	}
}

syswrite(RULES, $rules);
close(RULES);

# Finally, execute the real mv
print STDERR "Finally run ($real @ARGV))\n" if ($debug);
exit system(($real, @ARGV)) / 256;
#@END

#@BEGIN spy-install
#
# Spy on install invocations to detect where each file will be installed,
# and construct corresponding CScout directives
#

$real = which($0);

$origline = "install " . join(' ', @ARGV);
$origline =~ s/\n/ /g;

@ARGV2 = @ARGV;

my @excecutables = ();
my $dest;
my $length = @ARGV2;
my $counter = 0;

while (my $i = shift @ARGV2) {
	$counter++;
	# If -d option is used skip this command
	if ($i eq "-d") {
		last;
	}
	# If -t save next argument to directory
	if ($i eq "-t") {
		$dest = shift @ARGV2;
		$counter++;
	}
	if ($counter == $length) {
		if (length $dest) {
			if (-x $i && ! -d $i) {
				push(@excecutables, $i);
			}
		} else {
			$dest = $i;
		}
		last;
	}
	# If file is a real executable add it to array
	if (-x $i && ! -d $i) {
		push(@excecutables, $i);
	}
}

if (@excecutables) {
	foreach (@excecutables) {
		$rules .= "INSTALL $_ $dest\n";
	}
}

syswrite(RULES, $rules);
close(RULES);

# Finally, execute the real install
print STDERR "Finally run ($real @ARGV))\n" if ($debug);
exit system(($real, @ARGV)) / 256;
#@END

#@BEGIN COMMON
#
# Code common to all spy programs
#

use Cwd 'abs_path';

# Return the absolute path of a file, even if does not exist
# Under Cygwin Perl, abs_path on missing files aborts the program
# with an error
sub
robust_abs_path
{
	my ($file) = @_;
	if (-r $file) {
		return abs_path($file);
	} else {
		my ($ret);
		open my $out, '>', $file or die "Unable to create $file: $!\n";
		close $out;
		$ret = abs_path($file);
		unlink($file);
		return $ret;
	}
}

# Return the absolute path for prog, excluding our own path
sub which
{
	my ($prog) = @_;
	$prog =~ s,^.*/,,;
	my @dirs = split(/:/, $ENV{PATH});
	for my $d (@dirs) {
		next if ($d eq $ENV{CSCOUT_SPY_TMPDIR} || $d =~ m/ccache/);
		return "$d/$prog" if (-x "$d/$prog");
	}
	die "Unable to locate $prog in PATH $ENV{PATH}\n";
}

# Check the system exit status for errors, report, and exit on failure
sub
check_exit
{
	my ($command, $exit_status, $error_file) = @_;

	if ($exit_status && defined($error_file)) {
		open my $fh, '<', $error_file or die "Cannot open file '$error_file': $!";

		while (my $line = <$fh>) {
			print STDERR $line;
		}
	}

	if ($exit_status == -1) {
		print STDERR "Failed to execute $command command: $!\n";
		exit(1);
	} elsif ($exit_status & 127) {
		my $signal = ($exit_status & 127);
		print STDERR "Child $command died with signal $signal.\n";
		exit(1);
	} else {
		my $exit_code = $exit_status >> 8;
		if ($exit_code != 0) {
			print STDERR "Command $command failed with exit code $exit_code.\n";
			exit($exit_code);
		}
	}
}

#@END
