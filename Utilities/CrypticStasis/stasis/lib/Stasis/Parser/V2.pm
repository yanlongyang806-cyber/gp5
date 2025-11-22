# Copyright (c) 2008, Gian Merlino
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    1. Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#    2. Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR 
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF 
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

package Stasis::Parser::V2;

use strict;
use warnings;

use Stasis::Parser;
use Stasis::Event qw/%action_map :constants/;
our @ISA = "Stasis::Parser";

my @fspell      = qw/spellid spellname spellschool/;
my @fextraspell = qw/extraspellid extraspellname extraspellschool/;
my @fdamage     = qw/amount school resisted blocked absorbed critical glancing crushing/;
my @fdamage_wlk = qw/amount extraamount school resisted blocked absorbed critical glancing crushing/;
my @fmiss       = qw/misstype amount/;
my @fspellname  = qw/spellname/;
my @fheal       = qw/amount critical/;
my @fheal_wlk   = qw/amount extraamount critical/;
my @fenergize   = qw/amount powertype extraamount/;
my @faura       = qw/auratype amount/;
my @fenv        = qw/environmentaltype/;

# Returns compact hashes for v2 logs.
sub parse {
    my ($self, $line) = @_;
    
    # Pull the stamp out.
    my ($t, @col) = $self->_split( $line );
    if( !$t || @col < 7 ) {
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
    
    # Common processing
    my $action = $action_map{ shift @col };
    my $result = {
        action              => $action,
        actor               => shift @col,
        actor_name          => shift @col || "",
        actor_relationship  => hex shift @col,
        target              => shift @col,
        target_name         => shift @col || "",
        target_relationship => hex shift @col,
        t                   => $t,
    };
    
    $result->{target} = 0 if $result->{target} eq "0x0000000000000000";
    $result->{actor}  = 0 if $result->{actor}  eq "0x0000000000000000";
    
    # _split sometimes puts an extra quote mark in the last column
    $col[$#col] =~ s/"$// if @col;
    
    # Action specific processing
    if( $action == SWING_DAMAGE ) {
        if( @col <= 8 ) {
            @{$result}{@fdamage} = @col;
        } else {
            @{$result}{@fdamage_wlk} = @col;
        }
    } elsif( $action == SWING_MISSED ) {
        @{$result}{@fmiss} = @col;
    } elsif( 
        $action == RANGE_DAMAGE || 
        $action == SPELL_DAMAGE || 
        $action == SPELL_PERIODIC_DAMAGE || 
        $action == SPELL_BUILDING_DAMAGE ||
        $action == DAMAGE_SHIELD || 
        $action == DAMAGE_SPLIT
    ) {
        if( @col <= 11 ) {
            @{$result}{ (@fspell, @fdamage) } = @col;
        } else {
            @{$result}{ (@fspell, @fdamage_wlk) } = @col;
        }
    } elsif( 
        $action == RANGE_MISSED || 
        $action == SPELL_MISSED || 
        $action == SPELL_PERIODIC_MISSED || 
        $action == SPELL_CAST_FAILED || 
        $action == DAMAGE_SHIELD_MISSED
    ) {
        @{$result}{ (@fspell, @fmiss) } = @col;
    } elsif( $action == SPELL_HEAL || $action == SPELL_PERIODIC_HEAL ) {
        if( @col <= 5 ) {
            @{$result}{ (@fspell, @fheal) } = @col;
        } else {
            @{$result}{ (@fspell, @fheal_wlk) } = @col;
        }
    } elsif(
        $action == SPELL_PERIODIC_DRAIN ||
        $action == SPELL_PERIODIC_LEECH ||
        $action == SPELL_PERIODIC_ENERGIZE ||
        $action == SPELL_DRAIN ||
        $action == SPELL_LEECH ||
        $action == SPELL_ENERGIZE ||
        $action == SPELL_EXTRA_ATTACKS
    ) {
        @{$result}{ (@fspell, @fenergize) } = @col;
    } elsif(
        $action == SPELL_DISPEL_FAILED ||
        $action == SPELL_AURA_DISPELLED ||
        $action == SPELL_AURA_STOLEN ||
        $action == SPELL_INTERRUPT ||
        $action == SPELL_AURA_BROKEN_SPELL ||
        $action == SPELL_DISPEL ||
        $action == SPELL_STOLEN
    ) {
        @{$result}{ (@fspell, @fextraspell) } = @col;
    } elsif(
        $action == SPELL_AURA_APPLIED ||
        $action == SPELL_AURA_REMOVED ||
        $action == SPELL_AURA_APPLIED_DOSE ||
        $action == SPELL_AURA_REMOVED_DOSE ||
        $action == SPELL_AURA_REFRESH
    ) {
        @{$result}{ (@fspell, @faura) } = @col;
    } elsif(
        $action == ENCHANT_APPLIED ||
        $action == ENCHANT_REMOVED
    ) {
        @{$result}{@fspellname} = @col;
    } elsif( $action == ENVIRONMENTAL_DAMAGE ) {
        if( @col <= 9 ) {
            @{$result}{ (@fenv, @fdamage) } = @col;
        } else {
            @{$result}{ (@fenv, @fdamage_wlk) } = @col;
        }
    } elsif( $action ) {
        @{$result}{@fspell} = @col;
    }
    
    $result->{school} = hex $result->{school} if defined $result->{school};
    $result->{spellschool} = hex $result->{spellschool} if defined $result->{spellschool};
    $result->{extraspellschool} = hex $result->{extraspellschool} if defined $result->{extraspellschool};
    $result->{powertype} = hex $result->{powertype} if defined $result->{powertype};
    
    bless $result, "Stasis::Event";
}

my $stamp_regex = qr/^(\d+)\/(\d+) (\d+):(\d+):(\d+)\.(\d+)  (.*?)[\r\n]*$/s;
my $csv_regex = qr{"?,(?=".*?"(?:,|$)|[^",]+(?:,|$))"?};

sub _split {
    my ( $self, $line ) = @_;

    if( $line =~ $stamp_regex ) {
        return POSIX::mktime(
            $5,                      # sec
            $4,                      # min
            $3,                      # hour
            $2,                      # mday
            $1 - 1,                  # mon
            $self->{year} - 1900,    # year
            0,                       # wday
            0,                       # yday
            -1                       # is_dst
        ) + $6 / 1000, map { $_ eq "nil" ? "" : $_ } split $csv_regex, $7;
    } else {
        # Couldn't recognize time
        return 0, map { $_ eq "nil" ? "" : $_ } split $csv_regex, $line;
    }
}

1;
