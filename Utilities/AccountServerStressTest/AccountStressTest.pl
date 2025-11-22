use strict;
use Getopt::Long;
use Text::CSV;
use FindBin;
use Data::Dumper;
use Data::Serializer;
use XMLRPC::Lite;
use Time::HiRes qw(time usleep);
use Digest::SHA qw(sha256_hex);
use threads ('stack_size' => 64 * 1024);
use threads::shared;
$|=1;

# -------------------------------------------------------------------------------
# Constants - Please tweak!

sub XMLRPC_TIMEOUT()       { 60 * 5 }

#sub THREAD_COUNT()        { 1 }
#sub ACCOUNTS_PER_THREAD() { 1 }
#sub XMLRPC_HOST()         { "http://jdrago:8081/xmlrpc" }
#sub SUBSCRIPTION_NAME()   { 'StressTestSub' }

# -------------------------------------------------------------------------------
# Global Variables

my $gAttempts :shared; # Just for display while running, not really important

# -------------------------------------------------------------------------------
# Main

{
    # ---------------------------------------------------------------------------
    # CmdLine Options Gathering / Processing 
    my $opts = {};

    unless(GetOptions($opts,
                'mode=s',
                'seed=i',
                'hostName=s',
                'numThreads=i',
                'accountsPerThread=i',
                'subName=s',
                'prodName=s'
                ))
    {
        die "Failed to parse command line arguments.\n";
    }

    my $goodArgs = 1;
    unless(defined($opts->{'mode'})
       and defined($opts->{'seed'})
       and defined($opts->{'hostName'})
       and defined($opts->{'numThreads'})
       and defined($opts->{'accountsPerThread'})
       )
    {
    	print "ERROR: Missing at least one Required argument.\n";
    	$goodArgs = 0;
    }

    $opts->{'mode'} = lc($opts->{'mode'});
    if($opts->{'mode'} eq 'sub')
    {
        unless(defined($opts->{'subName'}))
        {
        	print STDERR "ERROR: Sub mode requires --subName.\n";
        	$goodArgs = 0;
        }
    }
    elsif($opts->{'mode'} eq 'trans')
    {
        unless(defined($opts->{'prodName'}))
        {
        	print STDERR "ERROR: Trans mode requires --prodName.\n";
        	$goodArgs = 0;
        }
    }
    else
    {
        print STDERR "ERROR: --mode must be either sub or trans.\n";
    	$goodArgs = 0;
    }

    unless($goodArgs)
    {
    	print STDERR "Syntax : AccountStressTest [arguments]\n";
    	print STDERR "\nRequired: \n";
    	print STDERR "    --mode [Sub|Trans]\n";
    	print STDERR "    --hostName string\n";
    	print STDERR "    --numThreads X\n";
    	print STDERR "    --accountsPerThread X\n";
    	print STDERR "    --seed X\n";
    	print STDERR "\nRequired For Sub Mode: \n";
    	print STDERR "    --subName string\n";
    	print STDERR "\nRequired For Trans Mode: \n";
    	print STDERR "    --prodName string\n";
    	exit;
    }

    $opts->{'url'} = "http://" . $opts->{'hostName'} . ":8081/xmlrpc";
    $opts->{'total'} = $opts->{'numThreads'} * $opts->{'accountsPerThread'};

    # ---------------------------------------------------------------------------
    # Lookup Product ID, if in Trans mode

    if($opts->{'mode'} eq 'trans')
    {
    	$opts->{'prodID'} = 1;
    }

    # ---------------------------------------------------------------------------
    # Create Threads

    my $seed = $opts->{'seed'};
    my $start = time();

    $gAttempts = 0;
    for(my $i=0; $i<$opts->{'numThreads'}; $i++)
    {
        my $t  = threads->create('ThreadProc', $seed, $opts);
        $seed += $opts->{'accountsPerThread'};
    }

    # ---------------------------------------------------------------------------
    # Wait for Threads

    my @results = ();

    while(1)
    {
        my @running = threads->list(threads::running);
        my $runningCount = scalar(@running);

        my @joinable = threads->list(threads::joinable);

        for my $j (@joinable)
        {
        	my $res = $j->join();
        	push @results, @$res;
        }

        printf STDERR "Running Threads: %d | Complete: %d / %d           \r", 
                      $runningCount, $gAttempts, $opts->{'total'};

        last if($runningCount == 0);

        sleep(1);
    }

    # ---------------------------------------------------------------------------
    # Report Results

    my $t = time() - $start;
    printf STDERR "\nComplete. [%2.2f total sec]\n", $t;


    my $currentTime = time;
    my $datafile;

    if(open($datafile, ">", MakeFilename($opts, $currentTime, 'raw')))
    {
        my $serializer = new Data::Serializer();
        print $datafile $serializer->serialize(\@results);
        close($datafile);
    }

    if(open($datafile, ">", MakeFilename($opts, $currentTime, 'txt')))
    {
    	printf $datafile "Total: %2.2f\n", $t;
        print $datafile Dumper(@results);
        close($datafile);
    }

    if(open($datafile, ">", MakeFilename($opts, $currentTime, 'csv')))
    {
        WriteCSV(\@results, $datafile);
        close($datafile);
    }
}

# -------------------------------------------------------------------------------
# Filename Creator

sub MakeFilename
{
	my($opts, $currentTime, $ext) = @_;

	my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime($currentTime);
	$year += 1900;

    return sprintf("%s/T%s%.2d%.2d-%.2d%.2d%.2d-%4.4d-%4.4d-%s.%s", 
                $FindBin::Bin, $year, $mon+1, $mday, $hour, $min, $sec, 
                $opts->{'numThreads'}, $opts->{'accountsPerThread'}, $opts->{'mode'},
                $ext);
}

# -------------------------------------------------------------------------------
# CSV Creator

sub WriteCSV
{
	my($list, $fh) = @_;
	my %hcol;

	for my $entry (@$list)
    {
    	for my $key (keys %$entry)
        {
        	$hcol{$key}++;
        }
    }
    
    my @cols = sort keys %hcol;
    my $csv = new Text::CSV;
    $csv->print($fh, \@cols);
    print $fh "\n";

	for my $entry (@$list)
    {
    	my @line;
    	for my $col (@cols)
        {
            my $val = $entry->{$col} // '';
            push @line, $val;
        }

        $csv->print($fh, \@line);
        print $fh "\n";
    }
}

# -------------------------------------------------------------------------------
# ThreadProc - Main entry point of each thread. Takes a seed value.

sub ThreadProc
{
	my($seed, $opts) = @_;
	
    my @results = ();
    for(my $i=0; $i<$opts->{'accountsPerThread'}; $i++)
    {
        my $result = RunTest($seed, $opts);
        $result->{'seed'} = $seed;
        push @results, $result;
        $seed++;
    }

    return \@results;
}

# -------------------------------------------------------------------------------

sub RunTest
{
	my($seed, $opts) = @_;

    my $user = CreateFakeUserData($seed, $opts);

    my $start      = time();
    my $ret        = CreateAccountAndSub($user, $seed, $opts);
    $ret->{'time'} = time() - $start;

    # locks auto-unlock when it leaves scope
    {
        lock($gAttempts);
        $gAttempts++;
    }

    return $ret;
}

# -------------------------------------------------------------------------------

sub CreateFakeUserData
{
    my ($seed, $opts) = (@_);

    my $userData = {
        accountName    => sprintf('stresstest%4.4d', $seed),
        passwordHash   => sha256_hex('test'),
        displayName    => sprintf('StressTest%4.4d', $seed),
        email          => sprintf('StressTest%4.4d@crypticstudios.com', $seed),
        firstName      => 'Stress',
        lastName       => 'Test',
        fullName       => 'Stress Test',
        dobYear        => int(rand(20)) + 1969, # Aged 20 to 40, roughly
        dobMonth       => int(rand(12)) + 1,
        dobDay         => int(rand(28)) + 1,

        currency       => 'USD',
        CVV2           => '123',
        expirationDate => '201202',
        creditCard     => '5500005555555559',
        address1       => '980 University Ave.',
        city           => 'Los Gatos',
        county         => 'Santa Clara',
        district       => 'CA',
        postalCode     => '95032',
        country        => 'US',
        phone          => '4083991969',
    };

    return $userData;
}

sub CreateAccountAndSub
{
    my ($user, $seed, $opts) = @_;
    my ($xcall, $transID);
    my $ret = { status => 'FAIL' };

    # ----------------------------------------------------------
    # Create Account

    $xcall = XCall($opts, $ret, 'CreateNewAccount',
            $user->{'accountName'},  # const char *accountName,
            $user->{'passwordHash'}, # const char *passwordHash,
            $user->{'displayName'},  # const char *displayName,
            $user->{'email'},        # const char *email,
            $user->{'firstName'},    # const char *firstName,
            $user->{'lastName'},     # const char *lastName,
            '',                      # const char *productKey,
            $user->{'dobYear'},      # int iYear,
            $user->{'dobMonth'},     # int iMonth,
            $user->{'dobDay'},       # int iDay,
            0,                       # int bSkipPrefixCheck,
            0                        # int bInternalLoginOnly
         );

    unless($xcall->{'result'})
    {
    	$ret->{'text'} = "[$seed] : CreateNewAccount failed";
    	return $ret;
    }

    unless($xcall->{'result'}->{'UserStatus'} eq 'user_update_ok')
    {
        $ret->{'text'} = "[$seed] : CreateNewAccount " . $xcall->{'result'}->{'UserStatus'};
    	return $ret;
    }

    my $ID = $xcall->{'result'}->{'ID'};

    if($opts->{'mode'} eq 'sub')
    {
        # ----------------------------------------------------------
        # Create a subscription

        $xcall = XCall($opts, $ret, 'SubCreate',
                $user->{'accountName'},  # const char *user,
                $opts->{'subName'},      # const char *subname,
                'USD',                   # const char *currency,
                {
                    active            => 1,
                    accountHolderName => $user->{'fullName'},

                    currency          => $user->{'currency'},
                    CVV2              => new XMLRPC::Data(type => 'string', value => $user->{'CVV2'}),
                    expirationDate    => new XMLRPC::Data(type => 'string', value => $user->{'expirationDate'}),
                    account           => new XMLRPC::Data(type => 'string', value => $user->{'creditCard'}),
                    addr1             => $user->{'address1'},
                    city              => $user->{'city'},
                    county            => $user->{'county'},
                    district          => $user->{'district'},
                    postalCode        => new XMLRPC::Data(type => 'string', value => $user->{'postalCode'}),
                    country           => $user->{'country'},
                    phone             => new XMLRPC::Data(type => 'string', value => $user->{'phone'}),

                },                        # UpdatePaymentMethod *pm
                "", 
                );

        unless($xcall->{'result'})
        {
            $ret->{'text'} = "[$seed] : SubCreate failed";
            return $ret;
        }

        $transID = $xcall->{'result'}->{'Transid'};
        unless($transID)
        {
            $ret->{'text'} = "[$seed] : SubCreate no transID";
            return $ret;
        }

        $xcall = WaitForTrans('SubCreate', $ret, $transID, $user, $seed, $opts);
        unless($xcall->{'result'})
        {
            $ret->{'text'} = "[$seed] : SubCreate-WaitForTrans failed";
            return $ret;
        }

        unless($xcall->{'result'}->{'Status'} eq 'SUCCESS')
        {
            $ret->{'text'} = "[$seed] : SubCreate Trans failed: " . $xcall->{'result'}->{'Resultstring'};
            return $ret;
        }
    }
    else
    {
        # ----------------------------------------------------------
        # Create a one time transaction

        $xcall = XCall($opts, $ret, 'Purchase',
                {
                    user             => $user->{'accountName'},
                    currency         => 'USD',
                    paymentMethod    => {
                        active            => 1,
                        accountHolderName => $user->{'fullName'},

                        currency          => $user->{'currency'},
                        CVV2              => new XMLRPC::Data(type => 'string', value => $user->{'CVV2'}),
                        expirationDate    => new XMLRPC::Data(type => 'string', value => $user->{'expirationDate'}),
                        account           => new XMLRPC::Data(type => 'string', value => $user->{'creditCard'}),
                        addr1             => $user->{'address1'},
                        city              => $user->{'city'},
                        county            => $user->{'county'},
                        district          => $user->{'district'},
                        postalCode        => new XMLRPC::Data(type => 'string', value => $user->{'postalCode'}),
                        country           => $user->{'country'},
                        phone             => new XMLRPC::Data(type => 'string', value => $user->{'phone'}),

                    },                        # UpdatePaymentMethod *pm
                    ProductID        => [ $opts->{'prodID'} ],
                } # XMLRPCPurchaseRequest *pRequest
                );

        unless($xcall->{'result'})
        {
            $ret->{'text'} = "[$seed] : Purchase failed";
            return $ret;
        }

        $transID = $xcall->{'result'}->{'Transid'};
        unless($transID)
        {
            $ret->{'text'} = "[$seed] : Purchase no transID";
            return $ret;
        }

        $xcall = WaitForTrans('Purchase', $ret, $transID, $user, $seed, $opts);
        unless($xcall->{'result'})
        {
            $ret->{'text'} = "[$seed] : Purchase-WaitForTrans failed";
            return $ret;
        }

        unless($xcall->{'result'}->{'Status'} eq 'SUCCESS')
        {
            $ret->{'text'} = "[$seed] : Purchase Trans failed: " . $xcall->{'result'}->{'Resultstring'};
            return $ret;
        }
    }

    $ret->{'status'} = 'PASS';
    return $ret;
}

sub WaitForTrans
{
	my ($which, $timeObj, $transID, $user, $seed, $opts) = @_;
	my $xcall;

    my $starttime = time();
    while(1)
    {
        $xcall = XCall($opts, $timeObj, 'TransView',
                $transID,  # const char *webUID
                );

        last unless($xcall->{'result'});
        last unless($xcall->{'result'}->{'Status'} eq 'PROCESS');

        sleep(1);
    }

    my $totalTime = time() - $starttime;
    $timeObj->{"timeWaitForTrans-$which"} = $totalTime;
    $timeObj->{'timeWaitForTrans'} += $totalTime;
    return $xcall;
}

sub XCall
{
    my $opts    = shift @_;
    my $timeObj = shift @_; # Object to add 'timeXCall'
    my $cmd     = shift @_;
    my @args = @_; # Everything else
    my $ret = {};
    
    my $start = time();
    my $r;

    #eval
    {
        $r = XMLRPC::Lite
        -> proxy($opts->{'url'}, timeout => XMLRPC_TIMEOUT)
        -> call($cmd, @args);
    };

    #if(defined($@))
    #{
    # 	print "\nXMLRPC FAILURE: '" . $@ . "' ... continuing...\n";
    # 	return {};
    #}

    my $totalTime = time() - $start;
    $timeObj->{"timeXCall-$cmd"} = $totalTime;
    $timeObj->{'timeXCall'} += $totalTime;

    if($r->fault)
    {
        print $r->fault->{'faultString'} . "\n";
    }
    else
    {
#       print "Result: " . Dumper($xcall->result) . "\n";
        $ret->{'result'} = $r->result;
    }

    return $ret;
}

