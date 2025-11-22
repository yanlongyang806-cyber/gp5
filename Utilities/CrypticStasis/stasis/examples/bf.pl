#!/usr/bin/perl -w

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

use strict;
use warnings;
use lib 'lib';
use open ':encoding(utf8)';
use Stasis::Parser;

die "Usage: $0 <logfile> <warriors>" unless @ARGV >= 2;

# Open the file
my $fh;
my $log = shift @ARGV;
open $fh, $log or die "Could not open file: $log";

my $parser = Stasis::Parser->new;

# This will track whether the Deep Wounds proc is up on each mob
my %bf;

# This will track how much physical damage each mob took, total
my %pdmg;

# This will track how much damage Blood Frenzy added to each mob
my %bfdmg;

# This will track mob names for display purposes.
my %index;

# Damage-dealing actions.
my %actions = (
    ENVIRONMENTAL_DAMAGE => 1,
    SWING_DAMAGE => 1,
    RANGE_DAMAGE => 1,
    SPELL_DAMAGE => 1,
    DAMAGE_SPLIT => 1,
    SPELL_PERIODIC_DAMAGE => 1,
    DAMAGE_SHIELD => 1,
);

my $nlog = 0;
$| = 1;
print "Processing...";
while( defined( my $line = <$fh> ) ) {
    # Every 50000 lines print.
    if( $nlog % 50000 == 0 ) {
        print " $nlog";
    }
    
    $nlog ++;
    
    my %entry = $parser->parse($line);

    # Track Deep Wounds proc status
    if( grep( $_ eq $entry{actor_name}, @ARGV ) && $actions{ $entry{action} } && $entry{extra}{critical} ) {
        $bf{ $entry{target} } = $entry{t} + 12;
    }

    if( $bf{ $entry{target} } && $bf{ $entry{target} } < $entry{t} ) {
        $bf{ $entry{target} } = 0;
    }

    # Track physical damage
    if( $actions{ $entry{action} } && $entry{extra}{amount} && $entry{extra}{spellschool} && $entry{extra}{spellschool} == 1 ) {
        if( $bf{ $entry{target} } ) {
            $bfdmg{ $entry{target} } += $entry{extra}{amount} - $entry{extra}{amount} / 1.04;
        }
        
        $pdmg{ $entry{target} } += $entry{extra}{amount};
    }
    
    $index{ $entry{actor} } ||= $entry{actor_name} if( $entry{actor} );
    $index{ $entry{target} } ||= $entry{target_name} if( $entry{target} );
}

print " $nlog\n";

foreach my $mob (sort { $index{$a} cmp $index{$b} } keys %bf) {
    printf "%s: %0.1f dmg (%0.1f%% physical dps increase)\n", 
        $index{$mob},
        $bfdmg{$mob} || 0,
        $pdmg{$mob} && $bfdmg{$mob} && ( $pdmg{$mob}/($pdmg{$mob}-$bfdmg{$mob}) - 1 )*100,
}
