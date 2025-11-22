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

package Stasis::Extension::Activity;

use strict;
use warnings;

use Stasis::Event qw/:constants/;
use Stasis::Extension qw/span_sum/;

our @ISA = "Stasis::Extension";

sub start {
    my $self = shift;
    $self->{actors} = {};
    $self->{span_scratch} = {};
    
    # No damage for this long will end a DPS span.
    $self->{_dpstimeout} = 5;
}

sub actions {
    map { $_ => \&process } qw/ENVIRONMENTAL_DAMAGE SWING_DAMAGE SWING_MISSED RANGE_DAMAGE RANGE_MISSED SPELL_DAMAGE DAMAGE_SPLIT SPELL_MISSED SPELL_PERIODIC_DAMAGE SPELL_PERIODIC_MISSED DAMAGE_SHIELD DAMAGE_SHIELD_MISSED/;
}

sub key {
    qw/actor target/;
}

sub value {
    qw/spans/;
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
    
    my $target = $event->{target};
    
    # Create a scratch hash for this actor/target pair if it does not exist already.
    $self->{span_scratch}{ $actor }{ $target } ||= pack "dd", 0, 0;
    
    # Track DPS time.
    my ($astart, $aend) = unpack "dd", $self->{span_scratch}{ $actor }{ $target };
    if( !$astart ) {
        # This is the first DPS action, so mark the start of a span.
        $astart = $event->{t};
        $aend = $event->{t};
    } elsif( $aend + $self->{_dpstimeout} < $event->{t} ) {
        # The last span ended, add it.
        $self->{actors}{ $actor }{ $target }{spans} ||= [];
        
        my $span = pack "dd", (
            $astart,
            $aend + $self->{_dpstimeout},
        );
        
        push @{$self->{actors}{ $actor }{ $target }{spans}}, $span;
        
        # Reset the start and end times to the current time.
        $astart = $event->{t};
        $aend = $event->{t};
    } else {
        # The last span is continuing.
        $aend = $event->{t};
    }
    
    # Store what we came up with.
    $self->{span_scratch}{ $actor }{ $target } = pack "dd", $astart, $aend;
}

sub finish {
    my $self = shift;
    
    # We need to close up all the un-closed dps spans.
    while( my ($kactor, $vactor) = each( %{ $self->{span_scratch} } ) ) {
        while( my ($ktarget, $vtarget) = each( %$vactor ) ) {
            $self->{actors}{ $kactor }{ $ktarget }{spans} ||= [];
            
            my ($vstart, $vend) = unpack "dd", $vtarget;
            my $span = pack "dd", (
                $vstart,
                $vend + $self->{_dpstimeout},
            );
            
            push @{$self->{actors}{ $kactor }{ $ktarget }{spans}}, $span;
        }
    }
    
    # Remove _dpstimeout from all last spans for each actor.
    while( my ($kactor, $vactor) = each (%{ $self->{actors} }) ) {
        my ( $t_last, $ref_last );
        while( my ($ktarget, $vtarget) = each (%$vactor) ) {
            foreach my $span (@{$vtarget->{spans}}) {
                my ($start, $end) = unpack "dd", $span;
                if( !$t_last || $end > $t_last ) {
                    $t_last = $end;
                    $ref_last = \$span;
                }
            }
        }
        
        if( $t_last ) {
            my ($start, $end) = unpack "dd", $$ref_last;
            $$ref_last = pack "dd", $start, $end - $self->{_dpstimeout};
        }
    }
    
    delete $self->{span_scratch};
}

1;
