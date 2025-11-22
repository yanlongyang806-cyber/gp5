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

package Stasis::Extension::Presence;

use strict;
use warnings;
use Stasis::Extension;
use Stasis::Parser;
use Stasis::Event qw/:constants/;

our @ISA = "Stasis::Extension";

sub start {
    my $self = shift;
    $self->{actors} = {};
    $self->{start} = {};
    $self->{end} = {};
    delete $self->{total};
}

sub actions {
    map { $_ => \&process } keys %Stasis::Parser::action_map;
}

sub key {
    qw/actor/;
}

sub value {
    qw/start end/;
}

sub process {
    my ( $self, $event ) = @_;
    
    my $guid;
    if( ( my $guid = $event->{actor} ) && $event->{action} != SPELL_AURA_REMOVED && $event->{action} != SPELL_AURA_REMOVED_DOSE ) {
        # AURA_REMOVE actions can be "action at a distance" and are not worth counting as presence.

        $self->{start}{ $guid } ||= $event->{t};
        $self->{end}{ $guid } = $event->{t};
    }
    
    if( my $guid = $event->{target} ) {
        $self->{start}{ $guid } ||= $event->{t};
        $self->{end}{ $guid } = $event->{t};
    }
}

sub finish {
    my ($self) = @_;
    
    foreach my $actor (keys %{$self->{start}}) {
        $self->{actors}{$actor} = {
            start => $self->{start}{$actor},
            end => $self->{end}{$actor},
        };
    }
    
    delete $self->{start};
    delete $self->{end};
}

sub sum {
    die "unsupported";
}

# Returns (start, end, total) for the raid or for an actor
sub presence {
    my ( $self, @actors ) = @_;

    if( @actors && ( @actors > 1 || $actors[0] ) ) {
        # Actor set other than the environment
        my $start = undef;
        my $end = undef;

        foreach my $actor (@actors) {
            if( $actor && $self->{actors}{$actor} ) {
                my ($istart, $iend) = ( $self->{actors}{$actor}{start}, $self->{actors}{$actor}{end} );
                if( !defined $start || $start > $istart ) {
                    $start = $istart;
                }

                if( !defined $end || $end < $iend ) {
                    $end = $iend;
                }
            }
        }
        
        return wantarray ? ( $start || 0, $end || 0, ($end || 0) - ($start || 0) ) : (($end||0) - ($start||0));
    } else {
        # Raid
        if( !$self->{total} ) {
            my ($start, $end) = (0, 0);
            foreach my $h (values %{ $self->{actors} }) {
                my ($istart, $iend) = ( $h->{start}, $h->{end} );
                $start = $istart if( !$start || $start > $istart );
                $end = $iend if( !$end || $end < $iend );
            }
            
            $self->{total} = {
                start => $start,
                end => $end,
            }
        }
        
        my ($tstart, $tend) = ( $self->{total}{start}, $self->{total}{end} );
        return wantarray ? ( $tstart, $tend, $tend - $tstart ) : ($tend-$tstart);
    }
}

1;
