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

package Stasis::EventDispatcher;

use strict;
use warnings;
use Carp;

use Stasis::Event;

sub new {
    my $class = shift;
    my %exts;
    my @handlers;

    # Initialize the handler arrays.
    $handlers[0] = [];
    foreach ( values %Stasis::Event::action_map ) {
        $handlers[$_] = [];
    }

    bless \@handlers, $class;
}

sub add {
    my ( $self, @actions ) = @_;
    my $f = pop @actions;
    
    # Listen for all actions if none are provided
    @actions = keys %Stasis::Event::action_map if ! @actions;

    if( ref $f eq 'CODE' ) {
        foreach my $action ( grep { $_ } map { $Stasis::Event::action_map{$_} } @actions ) {
            push @{ $self->[$action] }, $f;
        }
    } else {
        croak "null listener";
    }
}

sub remove {
    my ( $self, $f ) = @_;

    foreach my $action ( values %Stasis::Event::action_map ) {
        $self->[$action] = [ grep { $_ != $f } @{ $self->[$action] } ];
    }
}

sub process {
    my ( $self, $event ) = @_;

    foreach my $f ( @{ $self->[ $event->{action} ] } ) {
        $f->( $event );
    }
}

# Remove everything
sub flush {
    my ( $self ) = @_;

    $self->[$_] = [] foreach ( values %Stasis::Event::action_map );
}

1;
