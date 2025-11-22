use strict;
use Confluence;
use Data::Dumper;
use Term::UI;


print <<'EOL';
pdump.pl - Takes metadata from a wiki page and dumps it to a csv file.
EOL

my $url = "http://code:8081/rpc/xmlrpc";
my $user = '';
my $pass = '';
my $space = 'projectx';
my @inpages = ();
my $noprompt = 0;
my $verbose = 0;

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
metadump: takes wiki pages with metadata and makes an Excel spreadsheet

metadump [options] page_spec_1 page_spec_2 ...

   page_spec             - regular expression for page titles to look at.

   -user:username        - username for wiki
   -pass:password        - password for username
   -space:space key      - space where the pages exist
   -help                 - this help
   -noprompt             - don't prompt if all the info is given on the
                           command line
   -verbose              - print out what is going on
EOL
#'
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
		elsif ($arg =~ m/\-verbose/i)
		{
			$verbose = 1;
		}
		else
		{
			$inpages[@inpages] = $arg;
		}
	}
}

if(!($noprompt && $user && $pass && $space))
{
	$user = $term->get_reply(prompt=>'User:', default=>$user);
	$pass = $term->get_reply(prompt=>'Password:', default=>$pass);
	$space = $term->get_reply(prompt=>'Space:', default=>$space);
}

my $wiki = new Confluence($url, $user, $pass);
print "Getting page list...\n";
my $pages = $wiki->getPages($space);
my @pages = sort map { $_->{title}; } @$pages;
print "Done.\n";
my $title;

Confluence::setRaiseError(0);
Confluence::setPrintError(1);

foreach my $pagename (@inpages)
{
	print "   Working on $pagename...\n";
	my @subset = grep(/^$pagename/i, @pages);

	my @stuff = ();
	my %attribs = ();
	foreach $title (sort @subset)
	{
		my $page = $wiki->getPage($space, $title);
		if($page)
		{
			my ($metadata) = $page->{content}=~m/\{metadata-list\}\n*(.+)\{metadata-list\}/igs;

			next if(!$metadata);

			# hide |s inside of links.
			# This may be a problem in macros too, I guess.
			$metadata =~ s/(\[[^\]|]*)\|([^\]]*\])/$1~#~$2/gs;
			$metadata =~ s/({[^}|]*)\|([^}]*})/$1~#~$2/gs;

			my @lines = split(qq(\n), $metadata);
			my %metadata = map { s/~#~/|/g; $_; } map { s/"/""/g; $_; } map { m/^\|\s*(.*?)\s*\|\s*(.*?)\s*\|/} @lines;

			$metadata{_PageName} = $title;

			my @existing_labels = map { $_->{name} } @{ $wiki->getLabelsById($page->{id}) };
			$metadata{_Labels} = join(', ', @existing_labels);

			my $parent = $wiki->getPage($page->{parentId});
			$metadata{_Parent} = $parent->{title};

			push @stuff, { %metadata };
			map { $attribs{$_}++ if($metadata{$_}); } keys %metadata;
		}
	}

	my @attribs = reverse sort keys %attribs;
	my $csv;

	foreach my $attrib (@attribs)
	{
		$csv .= ',' if($csv);
		$csv .= qq("$attrib");
	}
	$csv .= "\n";

	foreach my $page (@stuff)
	{
		my $gotone = 0;
		foreach my $attrib (@attribs)
		{
			$csv .= ',' if($gotone);
			$csv .= qq("$page->{$attrib}");
			$gotone = 1;
		}
		$csv .= "\n";
	}

	$pagename =~ s/[*?:.|&]/_/g;
	open FILE, ">$pagename.csv";
	print FILE $csv;
	close FILE;
	print "   Done.\n";
}

END:
print "Done.\n";
$wiki->logout();

##########################################################################

