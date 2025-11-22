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

package Stasis::Extension::Aura;

use strict;
use warnings;

use Stasis::Extension;

our @ISA = "Stasis::Extension";

sub start {
    my $self = shift;
    $self->{actors} = {};
    $self->{targets} = {};
}

sub actions {
    SPELL_AURA_APPLIED => \&process_applied,
    SPELL_AURA_REMOVED => \&process_removed,
    
    # Events that don't have a source in WLK go here. This helps catch auras that are applied before an
    # encounter starts and then kept up.
    map( { $_ => \&process_refresh } qw/SPELL_AURA_REFRESH SPELL_AURA_APPLIED_DOSE SPELL_AURA_REMOVED_DOSE/ ),
    
    UNIT_DIED => \&process_death,
}

sub key {
    qw/actor spell target/;
}

sub value {
    qw/type spans/;
}

sub process_death {
    my ($self, $event) = @_;
    
    # Forcibly fade all auras when a unit dies.
    if( exists $self->{targets}{ $event->{target} } ) {
        foreach my $vaura (values %{ $self->{targets}{ $event->{target} } } ) {
            foreach my $vactor (values %$vaura) {
                if( @{ $vactor->{spans} } ) {
                    my ($start, $end) = unpack "dd", $vactor->{spans}[-1];

                    if( !$end ) {
                        $vactor->{spans}[-1] = pack "dd", $start, $event->{t};
                    }
                }
            }
        }
    }
}

sub process_applied {
    my ($self, $event) = @_;

    # Create a blank entry if none exists.
    # Stored "backwards", the person the aura is applied to (the target) is first.
    my $sdata = $self->{targets}{ $event->{target} }{ $event->{spellid} }{ $event->{actor} || 0 } ||= {
        gains => 0,
        fades => 0,
        type => undef,
        spans => [],
    };
    
    # Get the most recent span.
    my ($sstart, $send) = @{$sdata->{spans}} ? unpack( "dd", $sdata->{spans}->[-1] ) : (undef, undef);
    
    # An aura was gained, update the timeline.
    if( $send || !defined $sstart ) {
        # Either this is the first span, or the previous one has ended. We should make a new one.
        push @{$sdata->{spans}}, pack "dd", $event->{t}, 0;
        
        # In other cases, this means that we probably missed the fade message or this
        # is a dose application.
        
        # The best we can do in that situation is nothing, just keep the aura on even
        # though it may have faded at some point.
    }
    
    # Update the number of times this aura was gained.
    $sdata->{gains} ++;
    
    # Update the type of this aura.
    $sdata->{type} ||= $event->{auratype};
}

sub process_removed {
    my ($self, $event) = @_;
    
    # Create a blank entry if none exists.
    my $sdata = $self->{targets}{ $event->{target} }{ $event->{spellid} }{ $event->{actor} || 0 } ||= {
        gains => 0,
        fades => 0,
        type => undef,
        spans => [],
    };
    
    # Get the most recent span.
    my ($sstart, $send) = @{$sdata->{spans}} ? unpack( "dd", $sdata->{spans}->[-1] ) : (undef, undef);
    
    # An aura faded, update the timeline.
    if( defined $sstart && !$send ) {
        # We should end the most recent span.
        $sdata->{spans}->[-1] = pack "dd", $sstart, $event->{t};
    } else {
        # There is no span in progress, we probably missed the gain message.
        
        if( !$sdata->{gains} && !$sdata->{fades} ) {
            # If this is the first fade and there were no gains, let's assume it was up since 
            # before the log started (brave assumption)
            
            # If a process_refresh call caused a (0, 0) environment span to be created, take ownership of it. It would
            # only have been created if no auras were on the target, so odds are this remove event corresponds to that
            # aura.
            
            if( exists $self->{targets}{ $event->{target} }{ $event->{spellid} }{0} && @{$self->{targets}{ $event->{target} }{ $event->{spellid} }{0}{spans}} == 1 ) {
                my ($envstart, $envend) = unpack "dd", $self->{targets}{ $event->{target} }{ $event->{spellid} }{0}{spans}[0];
                delete $self->{targets}{ $event->{target} }{ $event->{spellid} }{0} if $envstart == 0 && $envend == 0;
            }
            
            push @{$sdata->{spans}}, pack "dd", 0, $event->{t};
        }
    }
    
    # Update the number of times this aura faded.
    $sdata->{fades} ++;
    
    # Update the type of this aura.
    $sdata->{type} ||= $event->{auratype};
}

sub process_refresh {
    my ($self, $event) = @_;
    
    # For refreshes, what we do is create an aura with "environment" as the source if and only if
    # this aura has not been seen on this target before.
    
    # As such, first check for pre-existing auras.
    return if exists $self->{targets}{ $event->{target} }{ $event->{spellid} };
    
    # OK, none exist. Create a new entry for the environment.
    # Stored "backwards", the person the aura is applied to (the target) is first.
    my $sdata = $self->{targets}{ $event->{target} }{ $event->{spellid} }{ 0 } = {
        gains => 0,
        fades => 0,
        type => $event->{auratype},
        spans => [
            pack "dd", 0, 0
        ],
    };
}

sub finish {
    my $self = shift;
    
    # Flip rows to get us an actors hash.
    while( my ($ktarget, $vtarget) = each(%{$self->{targets}}) ) {
        while( my ($kspell, $vspell) = each(%$vtarget) ) {
            while( my ($kactor, $vactor) = each(%$vspell) ) {
                # Add a reference to this leaf.
                $self->{actors}{ $kactor }{ $kspell }{ $ktarget } = $vactor;
                
                # Also delete gains and fades.
                delete $vactor->{gains};
                delete $vactor->{fades};
            }
        }
    }
}

1;
