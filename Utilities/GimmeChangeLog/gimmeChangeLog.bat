@rem = '--*-Perl-*--
@echo off
if "%OS%" == "Windows_NT" goto WinNT
perl -x -S "%0" %1 %2 %3 %4 %5 %6 %7 %8 %9
goto endofperl
:WinNT
n:\nobackup\bin\perl -x -S %0 %*
if NOT "%COMSPEC%" == "%SystemRoot%\system32\cmd.exe" goto endofperl
if %errorlevel% == 9009 echo You do not have Perl in your PATH.
if errorlevel 1 goto script_failed_so_exit_with_non_zero_val 2>nul
goto endofperl
@rem ';
#!perl
#line 15

use File::Find;

my $do_output;
my $output = "c:/comments_out.html";
my $spec;# = "data/Defs/NPC";
my $dir;# = "N:/revisions/$spec";
my $branch;# = "10";

my $start_day;# = 1;
my $start_month;# = 9;
my $start_year;# = 2005;

my $end_day;# = 8;
my $end_month;# = 11;
my $end_year;# = 2005;

if ( scalar(@ARGV) == 0 )
{
	print "parameters:\n";
	print "\t-startdate [date] - the earliest acceptable date, in the format [mon dd yyyy] (eg. Jul 10 2005)\n";
	print "\t-enddate [date] - the latest acceptable date, in the same format as -startdate\n";
	print "\t-branch [n] - the gimme branch, or update, to search in\n";
	print "\t-spec [spec] - a file spec to search (eg. data/defs/powers or *.txt)\n";
	exit;
}

#foreach $param ( @ARGV )
while (scalar(@ARGV) > 0)
{
	$param = shift@ARGV;
	#print "$param\n";
	#print "args: @ARGV\n";
	if ( $param eq '-startdate' )
	{
		$start_month = monthToNum(shift @ARGV);
		#print "$start_month\n";
		$start_day = shift @ARGV;
		#print "$start_day\n";
		$start_year = shift @ARGV;
		#print "$start_year\n";
	}
	elsif ( $param eq '-enddate' )
	{
		$end_month = monthToNum(shift @ARGV);
		#print "$end_month\n";
		$end_day = shift @ARGV;
		#print "$end_day\n";
		$end_year = shift @ARGV;
		#print "$end_year\n";
	}
	elsif ( $param eq '-branch' )
	{
		$branch = shift @ARGV;
	}
	elsif ( $param eq '-spec' )
	{
		$spec = shift @ARGV;
		#print "$spec\n";
	}
	#print "--args: @ARGV\n";
}

if ( $spec =~ /\*$/ )
{
	$dir = "N:/revisions/$spec" . "_vb#*.comments";
}
else
{
	$dir = "N:/revisions/$spec" . "*_vb#*.comments";
}
my $start_date_value = dateToDateVal($start_year, $start_month, $start_day);
my $end_date_value = dateToDateVal($end_year, $end_month, $end_day);


open( OUTPUT, "> $output" );

print "Finding comments in $dir...\n";
OutputHeader();
OutputTableStart();

@files = glob($dir);
#print scalar(@files) . "\n";

#($dir,$spec) = $dir =~ /(.*)([\*\?].*)/;

#print "dir: $dir\nspec: $spec\n";

#$spec =~ s/\./\\\./;
#$spec =~ s/\*/\.\*/;
#$spec =~ s/\?/\.{1}/;

#print "spec: $spec\n";


#find sub {
foreach $foundFile ( @files ) {
#	if (-f && $File::Find::name =~ /$branch\.comments$/i )
#	{
		#print "$File::Find::name\n";
		$file;
		@commentData;
		#($file) = $File::Find::name =~ /N:\/revisions\/(.+)_vb\#$branch\.comments/i;
		($file) = $foundFile =~ /N:\/revisions\/(.+)_vb\#$branch\.comments/i;

#use re 'eval';

#		if ($spec eq '' || $file =~ /${spec}/)
		if ( ! ($file eq '') )
		{

			print "FILE: $file\n";
			$do_output = 0;

			#open (COMMENTS, $File::Find::name);
			open (COMMENTS, $foundFile);
			while ( !eof(COMMENTS) )
			{
				$line = <COMMENTS>;
				$rev, $day, $month, $year, $hour, $min, $sec, $user, $comment;
				($rev, $user, $month, $day, $hour, $min, $sec, $year, $comment) = $line =~ 
					/\d+\t(\d+)\t(.+)\t[a-z]{3} ([a-z]{3}) (\d\d) (\d\d):(\d\d):(\d\d) (\d\d\d\d)\t.+\t(.*)/i;
				$month = monthToNum($month);

				$date_value = dateToDateVal( $year, $month, $day );
				if ($date_value >= $start_date_value && $date_value <= $end_date_value)
				{
					#print "$user, $month, $day, $hour, $min, $sec, $year, $comment\n"
					$commentHTML = MakeComment($user, $month, $day, $hour, $min, $sec, $year, $comment);
					#print "html: $commentHTML\n";
					push(@commentData, $commentHTML );
					$do_output = 1;
				}
				else
				{
					#print "Excluded: $month/$day/$year\n";
				}
			}

			if ($do_output == 1)
			{
				#print "Outputting Comments\n";
				OutputComments($file, @commentData);
				splice(@commentData, 0); # clear array
			}
		}
#	}
}
#}, $dir;

OutputTableEnd();
OutputFooter();


close(OUTPUT);
system( "\"$output\"");
sleep(1); # to prevent the file from being deleted before it opens
unlink("$output");

#system("pause");


sub dateToDateVal #args: $year, $month, $day
{
	my $yr = shift;
	my $mon = shift;
	my $dy = shift;

	return $yr * 10000 + $mon * 100 + $dy;
}

sub monthToNum
{
	my ($mon) = @_;
	return 1 if ( $mon =~ /jan/i );
	return 2 if ( $mon =~ /feb/i );
	return 3 if ( $mon =~ /mar/i );
	return 4 if ( $mon =~ /apr/i );
	return 5 if ( $mon =~ /may/i );
	return 6 if ( $mon =~ /jun/i );
	return 7 if ( $mon =~ /jul/i );
	return 8 if ( $mon =~ /aug/i );
	return 9 if ( $mon =~ /sep/i );
	return 10 if ( $mon =~ /oct/i );
	return 11 if ( $mon =~ /nov/i );
	return 12 if ( $mon =~ /dec/i );
	print "Bad value in monthToNum: $mon\n";
	return 987654321; # return an obviously bad value for any other argument
}

sub OutputHeader
{
	Output("<html><body>\n");
}

sub OutputTableStart
{
	Output("<table border=1 cellspacing=0 cellpadding=4>");
	Output("<tr><td bgcolor=\"black\"><font face=\"arial\" size=\"1\" color=\"white\">File</font></td>");
	Output("<td bgcolor=\"black\"><font face=\"arial\" size=\"1\" color=\"white\">User</font></td>");
	Output("<td bgcolor=\"black\"><font face=\"arial\" size=\"1\" color=\"white\">Timestamp</font></td>");
	Output("<td bgcolor=\"black\"><font face=\"arial\" size=\"1\" color=\"white\">Comment</font></td></tr>");
}
sub OutputTableEnd
{
	Output("</table>");
}

sub MakeComment #args: $user, $month, $day, $hour, $min, $sec, $year, $comment
{
	my $usr = shift;
	my $mon = shift;
	my $dy = shift;
	my $hr = shift;
	my $mn = shift;
	my $s = shift;
	my $yr = shift;
	my $com = shift;
	my $ret = "";

	$ret = $ret . "<td>$usr</td>";
	$ret = $ret . "<td>$mon/$dy/$yr $hr:$mn:$s</td>";
	$ret = $ret . "<td>$com</td>";
	return $ret;
}

sub OutputComments #args: $file, @comments
{
	my $fl = shift;
	my @coms = @_;
	my $numComs = scalar(@coms);
	my $first = 1;

	Output("<tr>");
	Output("<td rowspan = \"$numComs\">$fl</td>");

	#print $numComs . "\n";
	#print $file . "\n";

	foreach $com ( @coms )
	{
		#print "HTML: " . $com . "\n";
		if ( $first == 1 )
		{
			Output( "$com</tr>" );
			$first = 0;
		}
		else
		{
			Output( "<tr>$com</tr>" );
		}
	}
}

sub OutputFooter
{
	Output("</body></html>\n");
}

sub Output
{
	my $Out = shift;
	print OUTPUT ($Out);
#	print $Out;
}


