package Stasis::Parser::Cryptic;

use strict;
use warnings;

use Stasis::Parser;
use Time::HiRes;
use Stasis::Event qw/%action_map :constants/;
our @ISA = "Stasis::Parser";

my @fspell      = qw/spellid spellname spellschool/;
#my @fextraspell = qw/extraspellid extraspellname extraspellschool/;
#my @fdamage     = qw/amount school resisted blocked absorbed critical glancing crushing/;
my @fdamage_wlk = qw/amount extraamount school resisted blocked absorbed critical glancing crushing aoe/;
#my @fmiss       = qw/misstype amount/;
#my @fspellname  = qw/spellname/;
#my @fheal       = qw/amount critical/;
my @fheal_wlk   = qw/amount extraamount critical/;
#my @fenergize   = qw/amount powertype extraamount/;
#my @faura       = qw/auratype amount/;
#my @fenv        = qw/environmentaltype/;

use Data::Dumper;

my %gGUIDTable;
my %gSkillTable;
my $nextID = 1;

use Data::Dumper;

my %lasttp;
my $lastrawskillid;
my $lastrawsrcid;

# Returns compact hashes for v2 logs.
sub parse {
    my ($self, $line) = @_;

    chomp($line);
    $line =~ s/::/,/g;
    my @pieces = split(/,/, $line);
    my %e;
    @e{qw/time srcname rawsrcid u1 u2 dstname rawdstid skillname rawskillid type flags dmg/} = @pieces;

    my %flags;
    $flags{lc($_)}++ for(split(/\|/, $e{'flags'}));

    # * means use previous value
	if ($e{'rawdstid'} eq '*' ) 
	{
		$e{'rawdstid'} = $e{'u2'};
		$e{'dstname'} = $e{'u1'};
	}

	if ($e{'rawdstid'} eq '*' ) 
	{
		$e{'rawdstid'} = $e{'rawsrcid'};
		$e{'dstname'} = $e{'srcname'};
	}

    my %tp; # Time Pieces
	@tp{qw/year mon day hour min sec tsec/} = split(/[:,\.]/, $e{'time'});

    # Ugly!
    unless(
        $e{'rawsrcid'} 
    and $e{'rawdstid'} 
    and $e{'srcname'} 
    and $e{'dstname'} 
    and $e{'skillname'}
    and defined($tp{'year'})
    and defined($tp{'mon'})
    and defined($tp{'day'})
    and defined($tp{'hour'})
    and defined($tp{'min'})
    and defined($tp{'sec'})
    )
    {
        return {
            action              => 0,
            actor               => 0,
            actor_name          => "",
            actor_relationship  => 0,
            target              => 0,
            target_name         => "",
            target_relationship => 0,
        };
    }

	if (%tp eq %lasttp and $e{'rawskillid'} eq $lastrawskillid and $e{'rawsrcid'} eq $lastrawsrcid) 
	{
		$e{'aoe'} = 1;
	}
	%lasttp = %tp;
	$lastrawskillid = $e{'rawskillid'};
	$lastrawsrcid = $e{'rawsrcid'};

    @e{qw/srcid srcrel innerName srcmob/} = convertGUID($e{'rawsrcid'});
    @e{qw/dstid dstrel dstinnername dstmob/} = convertGUID($e{'rawdstid'});
    $e{'skillid'}        = convertSkillID($e{'rawskillid'});

    my $t = POSIX::mktime(
            $tp{'sec'},
            $tp{'min'},
            $tp{'hour'},
            $tp{'mon'} - 1,
            $tp{'day'},
            $tp{'year'} + 100,
            -1) + ($tp{'tsec'} / 10);

    my $result = {
    	cryptic             => 1,
        action              => $action_map{'SPELL_DAMAGE'},
        actor               => $e{'srcid'},
        actor_name          => $e{'srcname'}, #  . " (" . $e{'innerName'} . ")",
        actor_relationship  => $e{'srcrel'},
        target              => $e{'dstid'},
        target_name         => $e{'dstname'},
        target_relationship => $e{'dstrel'},	
        t                   => $t,
    };

    if($e{'type'} eq 'HitPoints')
    {
    	$result->{'action'} = $action_map{'SPELL_HEAL'};
        @{$result}{(@fspell, @fheal_wlk)} = (convertSkillID($e{'skillid'}),$e{'skillname'},64,-$e{'dmg'},0,0);
    }
    elsif (not ($e{'type'} eq 'Power'))
    {
    	$result->{'action'} = $action_map{'SPELL_DAMAGE'};
        @{$result}{(@fspell, @fdamage_wlk)} = (convertSkillID($e{'skillid'}),$e{'skillname'},64,$e{'dmg'},0,0,0,0,0,0,0,0,0,0,0,0,0,0);
    }
	else
	{
		return {
            action              => 0,
            actor               => 0,
            actor_name          => "",
            actor_relationship  => 0,
            target              => 0,
            target_name         => "",
            target_relationship => 0,
        };
	}

    if($flags{'critical'})
    {
        $result->{'critical'} = 1;
    }

	if($flags{'glancing'} or $flags{'block'})
    {
        $result->{'glancing'} = 1;
    }

    if($e{'srcmob'} or $e{'dstmob'})
    {
    	$result->{'mob'} = 1;
    }

	if ($e{'aoe'}) 
	{
		$result->{'aoe'} = 1;
	}

#    # Pull the stamp out.
#    my ($t, @col) = $self->_split( $line );
#    if( !$t || @col < 7 ) {
#        return {
#            action              => 0,
#            actor               => 0,
#            actor_name          => "",
#            actor_relationship  => 0,
#            target              => 0,
#            target_name         => "",
#            target_relationship => 0,
#        };
#    }
#    
#    # Common processing
#    my $action = $action_map{ shift @col };
#    my $result = {
#        action              => $action,
#        actor               => shift @col,
#        actor_name          => shift @col || "",
#        actor_relationship  => hex shift @col,
#        target              => shift @col,
#        target_name         => shift @col || "",
#        target_relationship => hex shift @col,
#        t                   => $t,
#    };
#    
#    $result->{target} = 0 if $result->{target} eq "0x0000000000000000";
#    $result->{actor}  = 0 if $result->{actor}  eq "0x0000000000000000";
#    
#    # _split sometimes puts an extra quote mark in the last column
#    $col[$#col] =~ s/"$// if @col;
#    
#    # Action specific processing
#    if( $action == SWING_DAMAGE ) {
#        if( @col <= 8 ) {
#            @{$result}{@fdamage} = @col;
#        } else {
#            @{$result}{@fdamage_wlk} = @col;
#        }
#    } elsif( $action == SWING_MISSED ) {
#        @{$result}{@fmiss} = @col;
#    } elsif( 
#        $action == RANGE_DAMAGE || 
#        $action == SPELL_DAMAGE || 
#        $action == SPELL_PERIODIC_DAMAGE || 
#        $action == SPELL_BUILDING_DAMAGE ||
#        $action == DAMAGE_SHIELD || 
#        $action == DAMAGE_SPLIT
#    ) {
#        if( @col <= 11 ) {
#            @{$result}{ (@fspell, @fdamage) } = @col;
#        } else {
#            @{$result}{ (@fspell, @fdamage_wlk) } = @col;
#        }
#    } elsif( 
#        $action == RANGE_MISSED || 
#        $action == SPELL_MISSED || 
#        $action == SPELL_PERIODIC_MISSED || 
#        $action == SPELL_CAST_FAILED || 
#        $action == DAMAGE_SHIELD_MISSED
#    ) {
#        @{$result}{ (@fspell, @fmiss) } = @col;
#    } elsif( $action == SPELL_HEAL || $action == SPELL_PERIODIC_HEAL ) {
#        if( @col <= 5 ) {
#            @{$result}{ (@fspell, @fheal) } = @col;
#        } else {
#            @{$result}{ (@fspell, @fheal_wlk) } = @col;
#        }
#    } elsif(
#        $action == SPELL_PERIODIC_DRAIN ||
#        $action == SPELL_PERIODIC_LEECH ||
#        $action == SPELL_PERIODIC_ENERGIZE ||
#        $action == SPELL_DRAIN ||
#        $action == SPELL_LEECH ||
#        $action == SPELL_ENERGIZE ||
#        $action == SPELL_EXTRA_ATTACKS
#    ) {
#        @{$result}{ (@fspell, @fenergize) } = @col;
#    } elsif(
#        $action == SPELL_DISPEL_FAILED ||
#        $action == SPELL_AURA_DISPELLED ||
#        $action == SPELL_AURA_STOLEN ||
#        $action == SPELL_INTERRUPT ||
#        $action == SPELL_AURA_BROKEN_SPELL ||
#        $action == SPELL_DISPEL ||
#        $action == SPELL_STOLEN
#    ) {
#        @{$result}{ (@fspell, @fextraspell) } = @col;
#    } elsif(
#        $action == SPELL_AURA_APPLIED ||
#        $action == SPELL_AURA_REMOVED ||
#        $action == SPELL_AURA_APPLIED_DOSE ||
#        $action == SPELL_AURA_REMOVED_DOSE ||
#        $action == SPELL_AURA_REFRESH
#    ) {
#        @{$result}{ (@fspell, @faura) } = @col;
#    } elsif(
#        $action == ENCHANT_APPLIED ||
#        $action == ENCHANT_REMOVED
#    ) {
#        @{$result}{@fspellname} = @col;
#    } elsif( $action == ENVIRONMENTAL_DAMAGE ) {
#        if( @col <= 9 ) {
#            @{$result}{ (@fenv, @fdamage) } = @col;
#        } else {
#            @{$result}{ (@fenv, @fdamage_wlk) } = @col;
#        }
#    } elsif( $action ) {
#        @{$result}{@fspell} = @col;
#    }
#    
#    $result->{school} = hex $result->{school} if defined $result->{school};
#    $result->{spellschool} = hex $result->{spellschool} if defined $result->{spellschool};
#    $result->{extraspellschool} = hex $result->{extraspellschool} if defined $result->{extraspellschool};
#    $result->{powertype} = hex $result->{powertype} if defined $result->{powertype};
    
    bless $result, "Stasis::Event";
    return $result;
}

sub convertGUID
{
	my($id) = @_;

	if($gGUIDTable{$id})
    {
    	return @{$gGUIDTable{$id}};
    }

    # Attempting to mimic data from http://www.wowwiki.com/API_UnitGUID

    my ($type, $innerName) = ($id =~ /^(.)\[\S+\s+([^\[]+)\]/);
    my $rel;

    my $newid = "0x00"; # AA
    my $mob = 0;

    if($type eq 'C')
    {
        # Critter
        $newid .= "3"; # B
        $rel = 0x10a48;
        $mob = 1;
    }
    else
    {
        # Player
        $newid .= "0"; # B
        $rel = 0x512;
    }

    $newid .= "0000000"; # CCCDDDD
    $newid .= sprintf('%5.5x', $nextID++); # EEEEEE

    my @ret = ($newid, $rel, $innerName, $mob);
    $gGUIDTable{$id} = \@ret;
    return @ret;
}

sub convertSkillID
{
	my($id) = @_;
	if($gSkillTable{$id})
    {
        return $gSkillTable{$id};
    }

    my $newid = sprintf('%d', $nextID++);
    $gSkillTable{$id} = $newid;
    return $newid;
}

1;
