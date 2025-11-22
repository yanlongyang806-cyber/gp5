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

package Stasis::Extension::Damage;

use strict;
use warnings;
use Carp;

use Stasis::Extension;
use Stasis::Event qw/:constants/;

our @ISA = "Stasis::Extension";

sub start {
    my $self = shift;
    $self->{actors} = {};
    $self->{targets} = {};
}

sub actions {
    map { $_ => \&process } qw/ENVIRONMENTAL_DAMAGE SWING_DAMAGE SWING_MISSED RANGE_DAMAGE RANGE_MISSED SPELL_DAMAGE DAMAGE_SPLIT SPELL_MISSED SPELL_PERIODIC_DAMAGE SPELL_PERIODIC_MISSED DAMAGE_SHIELD DAMAGE_SHIELD_MISSED/;
}

sub key {
    qw/actor spell target/;
}

sub value {
    qw/count hitCount hitTotal hitMin hitMax critCount critTotal critMin critMax tickCount tickTotal tickMin tickMax tickCritCount partialResistCount partialBlockCount partialAbsorbCount partialResistTotal partialBlockTotal partialAbsorbTotal crushing glancing dodgeCount absorbCount resistCount parryCount missCount blockCount reflectCount deflectCount immuneCount/;
}

sub process {
    my ($self, $event) = @_;
    
    # This was a damage event, or an attempted damage event.
    
    # We are going to take some liberties with environmental damage and white damage in order to get them
    # into the neat actor > spell > target framework. Namely an abuse of actor IDs and spell IDs (using
    # "0" as an actor ID for the environment and using "0" for the spell ID to signify a white hit). These
    # will both fail to look up in Index, but that's okay.
    my $actor;
    my $spell;
    if( $event->{action} == ENVIRONMENTAL_DAMAGE ) {
        $actor = 0;
        $spell = 0;
    } elsif( $event->{action} == SWING_DAMAGE || $event->{action} == SWING_MISSED ) {
        $actor = $event->{actor};
        $spell = 0;
    } else {
        $actor = $event->{actor};
        $spell = $event->{spellid};
    }
    
    # Get the spell hash.
    my $ddata = ($self->{actors}{ $actor }{ $spell }{ $event->{target} } ||= {});
    
    # Add to targets.
    $self->{targets}{ $event->{target} }{ $spell }{ $actor } ||= $ddata;
    
    # Add to the count.
    if ($event->{aoe} != 1)
    {    
	    $ddata->{count} += 1;
    }
    
    # Check if this was a hit or a miss.
    if( $event->{amount} && !$event->{misstype} ) {
        # HIT
        # Classify the damage WWS-style as a "hit", "crit", or "tick".
        my $type;
        if( $event->{action} == SPELL_PERIODIC_DAMAGE ) {
            $type = "tick";
            
            # keep track of tick crit rate, now that it's possible
            if( $event->{critical} ) {
                $ddata->{"tickCritCount"} ++;
            }
        } elsif( $event->{critical} ) {
            $type = "crit";
        } else {
            $type = "hit";
        }
                
        # Add the damage to the total for this type of hit (hit/crit/tick).
        if ($event->{aoe} != 1)
        {
	       	$ddata->{"${type}Count"} += 1;
	       	$ddata->{crushing}++ if $event->{crushing};
        	$ddata->{glancing}++ if $event->{glancing};
        }
        $ddata->{"${type}Total"} += $event->{amount};
        
        # Update min/max hit size.
        $ddata->{"${type}Min"} = $event->{amount}
            if( 
                !$ddata->{"${type}Min"} ||
                $event->{amount} < $ddata->{"${type}Min"}
            );

        $ddata->{"${type}Max"} = $event->{amount}
            if( 
                !$ddata->{"${type}Max"} ||
                $event->{amount} > $ddata->{"${type}Max"}
            );
        
        # Add any mods.
        if( $event->{blocked} ) {
            if ($event->{aoe} != 1)
            {
            	$ddata->{partialBlockCount} ++;
            }
            $ddata->{partialBlockTotal} += $event->{blocked};
        }
        
        if( $event->{resisted} ) {
            if ($event->{aoe} != 1)
            {
            	$ddata->{partialResistCount} ++;
            }
            $ddata->{partialResistTotal} += $event->{resisted};
        }
        
        if( $event->{absorbed} ) {
            if ($event->{aoe} != 1)
            {
            	$ddata->{partialAbsorbCount} ++;
            }
            $ddata->{partialAbsorbTotal} += $event->{absorbed};
        }
      
    } elsif( $event->{misstype} ) {
        # MISS
        $ddata->{ lc( $event->{misstype} ) . "Count" }++;
    }
}

1;
