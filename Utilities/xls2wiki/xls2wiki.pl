@rem = '--*-Perl-*--
@echo off
if "%OS%" == "Windows_NT" goto WinNT
n:\bin\perl -x -S "%0" %1 %2 %3 %4 %5 %6 %7 %8 %9
goto endofperl
:WinNT
n:\bin\perl -x -S "%0" %*
if NOT "%COMSPEC%" == "%SystemRoot%\system32\cmd.exe" goto endofperl
if %errorlevel% == 9009 echo You do not have Perl in your PATH.
if errorlevel 1 goto script_failed_so_exit_with_non_zero_val 2>nul
goto endofperl
@rem ';
#!perl
#line 15
#################################################################
#
# Build with pp -c -x -o xls2wiki.exe xls2wiki.pl
#

####
#
#   1 sposniewski  Initial revision
#   2 sposniewski  Change carriage returns to \\s in fields, remove MS crap
#                  from strings.
#   3 sposniewski  Support Excel 2007 (xlsx) files.
#
$rev = 3;

print qq(xls2wiki (rev $rev)\n   Take an Excel file and make a bunch of wiki metadata pages from it.\n);

use strict;
use Confluence;
use Data::Dumper;
use Term::UI;
use Spreadsheet::ParseExcel;
use Spreadsheet::XLSX;


my $wikiurl = "http://crypticwiki:8081/rpc/xmlrpc";
my $user = '';
my $pass = '';
my $space = 'projectx';
my $DefaultInputFile = 'powers_meta.xlsx';
my $verbose = 0;
my $noprompt = 0;

my $delete_obsolete = 0;  # should obsolete attribs be removed from the page

my @InputFiles = ();

my $term = new Term::ReadLine::Perl;

if (@ARGV > 0)
{
	# We only take one, but it'll be the last
	foreach my $arg (@ARGV)
	{
		if ($arg =~ m/\-help/i
			|| $arg =~ m/\-\?/i )
		{
			print <<'EOL';
xlstowiki: Take an Excel file and make a bunch of wiki metadata pages from it

xlstowiki [options] file1.xlsx file2.xlsx ...

   -user:username        - username for wiki edits
   -pass:password        - password for username
   -space:space key      - space where the pages will be added or modified
   -delete_obsolete      - remove obsolete fields from the table
   -verbose              - print out what is going on
   -noprompt             - don't prompt for user, password, and space

This program takes a spreadsheet and generates wiki pages and metadata based
on that spreadsheet. Each row in the spreadsheet refers to a page in the
wiki.

Somewhere in the spreadsheet there needs to be a series of headers. The
one required header is "_PageName". This is the name of the page that row
refers to. Any other cells in the same row as "_PageName" are considered
field names. These names are the fields which will be inserted into the wiki.
The names can be any string. Names beginning with underscore (_) are not
put into the wiki directly.

There are some special field names this program recognizes:
   _PageName       A required field name. Gives the name of the page.
   _Parent         Gives the name of the parent for the page
   _Labels         A comma separated list of labels to be applied to the page
   _DefaultLabels  A comma separated list of labels to also be applied

Each row starting with the row after the _PageName row which has a value
for _PageName will have a page made for it. If the _PageName is _END_, the
program stops (so you can ignore data under the table if you wish).

If the page already exists, the data in the spreadsheet is merged with the
data on the page. Anything on the wiki page which isn't part of the
{metadata-list}...{metadata-list} block is left unchanged. The metadata block
is updated with the fields in the spreadsheet. If the spreadsheet has an
asterisk (*) as its value, then the value in the page is kept. If a field
is blank on the spreadsheet, it will blank out the field on the page.

Asterisks can be used for the _Parent, _DefaultLabels, and _Labels fields
also. If you put a * in one of the label fields, you need to do them both.

If the page has extra fields the spreadsheet does not have, these fields are
put at the bottom of the metadata block. Give the -delete_obsolete option
if you wish these fields to be deleted instead.

You cannot delete pages with this program.

EOL
# '
			exit;
		}
		elsif ($arg =~ m/\-noprompt/i)
		{
			$noprompt = 1;
		}
		elsif ($arg =~ m/\-user:/i)
		{
			($user) = $arg=~m/\-user:(.*)/i;
		}
		elsif ($arg =~ m/\-pass:/i)
		{
			($pass) = $arg=~m/\-pass:(.*)/i;
		}
		elsif ($arg =~ m/\-space:/i)
		{
			($space) = $arg=~m/\-space:(.*)/i;
		}
		elsif ($arg =~ m/\-delete_obsolete/i)
		{
			$delete_obsolete = 1;
		}
		elsif ($arg =~ m/\-verbose/i)
		{
			$verbose = 1;
		}
		else
		{
			$arg =~ s'\\'/'g; # normalize slashes

			if (!(-e $arg))
			{
				print "ERROR: File: \"$arg\" does not exist.\n";
				exit;
			}
			if (!($arg =~ m/.+\.xlsx?$/i))
			{
				print "ERROR: File: \"$arg\" is not an Excel sheet.\n";
				exit;
			}

			$InputFiles[@InputFiles] = $arg;
		}
	}
}

if(!($noprompt && $user && $pass && $space))
{
	$user = $term->get_reply(prompt=>'User:', default=>$user);
	$pass = $term->get_reply(prompt=>'Password:', default=>$pass);
	$space = $term->get_reply(prompt=>'Space:', default=>$space);
}

if(!$InputFiles[0])
{
	my $file = $term->get_reply(prompt=>'File:', default=>$DefaultInputFile);
	$InputFiles[@InputFiles] = $file;
}

my $wiki = new Confluence($wikiurl, $user, $pass);

foreach my $file (@InputFiles)
{
	ProcessSpreadsheet($wiki, $file);
}

print "Done.\n";
$wiki->logout();

###########################################################################
###########################################################################
###########################################################################

sub ProcessSpreadsheet
{
	my $wiki = shift;
	my $file = shift;

	print "\n";
	print "Opening workbook $file.\n";
	if(!-e $file)
	{
		print qq(ERROR: $file doesn't exist.\n);
		return;
	}

	my $oBook;
	if($file =~ m/\.xlsx/)
	{
		$oBook = Spreadsheet::XLSX->new($file);
	}
	else
	{
		$oBook = Spreadsheet::ParseExcel::Workbook->Parse($file);
	}


	foreach my $sheet (@{$oBook->{Worksheet}})
	{
		#
		# skip worksheets beginning with an underscore
		#
		next if ($sheet->{Name} =~ /^_.*/);

		print qq(   $sheet->{Name}\n);

		my $start_row = -1;
		my $pagename_col = -1;

		if(!defined($sheet->{MaxRow}) || !defined($sheet->{MaxCol}))
		{
			print qq(      Blank worksheet, skipping.\n) if($verbose);
			next;
		}

		for(my $row = $sheet->{MinRow}; $row <= $sheet->{MaxRow} && $start_row<0; $row++)
		{
			for(my $col = $sheet->{MinCol}; $col <= $sheet->{MaxCol} && $start_row<0; $col++)
			{
				my $cell = $sheet->{Cells}[$row][$col];
				if ($cell && $cell->Value=~m/_PageName/i)
				{
					# OK, the row which has pagename in it is the row
					#    with all the attribute names in it.
					# All the rows follow which have a page name are
					#    turned into pages (until an _END is found)
					$start_row = $row;
					$pagename_col = $col;

				}
			}
		}

		if($start_row<0)
		{
			print qq(      No _PageName column found, skipping worksheet.\n) if($verbose);
			next;
		}

		#
		# Get the list of all the attribs.
		#

		my %attribs = (); # map names to column numbers
		my @attribs = (); # ordered list of attributes

		for(my $col = $sheet->{MinCol}; $col <= $sheet->{MaxCol}; $col++)
		{
			my $cell = $sheet->{Cells}[$start_row][$col];
			next if(!$cell || !$cell->Value);

			$attribs{$cell->Value} = $col;
			push @attribs, $cell->Value;
		}

		#
		# Visit each row for each of the attribs and do the voodoo
		#

		for(my $row = $start_row+1; $row <= $sheet->{MaxRow}; $row++)
		{
			my $cell = $sheet->{Cells}[$row][$pagename_col];
			next if(!$cell || !$cell->Value);

			my $pagename = $cell->Value;

			next if(!$pagename);
			last if($pagename=~m/^_end/i);

			my %page_metadata = (); # This is the metadata already on the page.

			#
			# Get the current page if it exists.
			#
			Confluence::setRaiseError(0);
			Confluence::setPrintError(0);

			my $page = $wiki->getPage($space, $pagename);
			if(Confluence::lastError())
			{
				if(Confluence::lastError()=~m/does not exist/i)
				{
					# New page
					$page = {
						space => $space,
						title => $pagename,
					};
				}
				else
				{
					die Confluence::lastError();
				}
			}
			else
			{
				#
				# Get the attribs from the page, if there is any.
				#
				my ($metadata) = $page->{content}=~m/\{metadata-list\}\n*(.+)\{metadata-list\}/igs;

				# hide |s inside of links.
				# This may be a problem in macros too, I guess.
				$metadata =~ s/(\[[^\]|]*)\|([^\]]*\])/$1~#~$2/gs;
				$metadata =~ s/({[^}|]*)\|([^}]*})/$1~#~$2/gs;

				my @lines = split(qq(\n), $metadata);
				%page_metadata = map { s/~#~/|/g; $_; } map { m/^\|\s*(.*?)\s*\|\s*(.*?)\s*\|/} @lines;
			}

			Confluence::setRaiseError(1);
			Confluence::setPrintError(1);

			#
			# Get the new attribs from the spreadsheet
			#
			my %new_metadata = ();

			foreach my $attrib (@attribs)
			{
				$cell = $sheet->{Cells}[$row][$attribs{$attrib}];
				if(!$cell)
				{
					$new_metadata{$attrib}='';
				}
				elsif($cell->Value ne '*')
				{
					my $value=FixMicrosoftCruft($cell->Value);
					$value=~s[\n][ \\\\]gs;

					$new_metadata{$attrib}=$value;
				}
				else
				{
					# Take the existing data from the page if there is any
					if(exists($page_metadata{$attrib}))
					{
						$new_metadata{$attrib}=$page_metadata{$attrib};
					}
				}

				# Get rid of page metadata value. Any remaining attribs from
				#   the page will get written as "Obsolete Fields"
				delete $page_metadata{$attrib};
			}

			# Now we have the new metadata merged with the old metadata.
			# Old metadata which is not in the spreadsheet's list of attribs
			#   is still in $metadata.

			#
			# Set the parent ID
			#
			my $parent;
			my $new_parent = 0;
			if(exists($new_metadata{_Parent}) && $new_metadata{_Parent} ne '*')
			{
				$parent = $wiki->getPage($space, $new_metadata{_Parent});
				if($parent)
				{
					if($page->{parentId} != $parent->{id})
					{
						$page->{parentId} = $parent->{id};
						$new_parent = 1;
					}
				}
				else
				{
					# The parent page doesn't exist. Don't make this one.
					print("      $page->{title}: Parent page ($new_metadata{_Parent}) doesn't exist. Didn't make page.\n");
				}
			}

			#
			# Make the metadata-list content block
			#

			# First the official attribs
			my $table = "{metadata-list}\n";
			foreach my $attrib (@attribs)
			{
				if($attrib!~m/^_/)
				{
					$table .= "|$attrib|$new_metadata{$attrib}|\n";
				}
			}

			# Put the obsolete ones at the end
			if(!$delete_obsolete)
			{
				my $obsolete = '';
				foreach my $attrib (sort keys %page_metadata)
				{
					if($attrib && $attrib!~m/Obsolete Fields/)
					{
						$obsolete .= "|$attrib|$page_metadata{$attrib} |\n";
					}
				}

				if($obsolete)
				{
					$table .= "|Obsolete Fields|" . localtime() . "|\n" . $obsolete;
				}

			}
			$table .= "{metadata-list}\n";
			#
			# Update the content of the page
			#
			my $content = $page->{content};

			#
			# Update the metadata table inside the page content
			#
			if(0 == $content=~s/(.*)\{metadata-list\}.*\{metadata-list\}\n*(.*)/$1$table$2/igs)
			{
				$content = $table . $content;
			}

			if($page->{content} eq $content && !$new_parent)
			{
				print("      $page->{title}: Unchanged\n") if($verbose);
			}
			else
			{
				print("      $page->{title}: Updating\n") if($verbose);

				$page->{content} = $content;

				# Finally, update the page
				$wiki->updatePage($page);
			}

			#
			# Update the labels for the page
			#
			if((exists($new_metadata{_Labels}) && $new_metadata{_Labels} && $new_metadata{_Labels} ne '*')
				|| (exists($new_metadata{_DefaultLabels}) && $new_metadata{_DefaultLabels} && $new_metadata{_DefaultLabels} ne '*'))
			{
				# OK, now add any labels
				#   (Need to reget page in case it's new. We need the ID)
				$page = $wiki->getPage($space, $pagename);

				if($page)
				{
					my %existing_labels = map { $_->{name}=>1 } @{ $wiki->getLabelsById($page->{id}) };

					my @labels;

					if(exists($new_metadata{_Labels}) && $new_metadata{_Labels} && $new_metadata{_Labels} ne '*')
					{
						@labels = split(/\s*,\s*/, $new_metadata{_Labels});
					}
					if(exists($new_metadata{_DefaultLabels}) && $new_metadata{_DefaultLabels} && $new_metadata{_DefaultLabels} ne '*')
					{
						push @labels, split(/\s*,\s*/, $new_metadata{_DefaultLabels});
					}

					foreach my $label (@labels)
					{
						if(exists($existing_labels{$label}))
						{
							delete $existing_labels{$label};
						}
						else
						{
							print("         Adding label: $label\n") if($verbose);
							$wiki->addLabelByName($label, $page->{id});
						}
					}

					# remove any which are left over
					foreach my $label (keys %existing_labels)
					{
						print("         Removing label: $label\n") if($verbose);
						$wiki->removeLabelByName($label, $page->{id});
					}
				}
			}
		}
	}
}

sub FixMicrosoftCruft
{
	my $s = shift;

	$s =~ s/\x82/,/g;
	$s =~ s/\x84/,,/g;
	$s =~ s/\x85/.../g;

	$s =~ s/\x88/^/g;

	$s =~ s/\x8B/</g;
	$s =~ s/\x8C/Oe/g;

	$s =~ s/\x19/'/g;
	$s =~ s/\x91/`/g;
	$s =~ s/\x92/'/g;
	$s =~ s/\x93/"/g;
	$s =~ s/\x94/"/g;
	$s =~ s/\x95/*/g;
	$s =~ s/\x96/-/g;
	$s =~ s/\x97/--/g;

	$s =~ s/\x9B/>/g;
	$s =~ s/\x9C/oe/g;

	return $s;
}
