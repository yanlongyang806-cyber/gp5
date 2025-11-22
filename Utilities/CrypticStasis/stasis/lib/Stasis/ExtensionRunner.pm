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

package Stasis::ExtensionRunner;

use strict;
use warnings;
use Stasis::Extension;

sub new {
    my $class = shift;
    my %exts;

    foreach (@_) {
        my $ext = Stasis::Extension->factory($_);
        $exts{$_} = $ext;
    }
    
    bless \%exts, $class;
}

sub start {
    my ($self, $ed) = @_;
    foreach my $ext (values %$self) {
        $ext->start;
        $ext->register( $ed );
    }
}

sub suspend {
    my ($self, $ed) = @_;
    foreach my $ext (values %$self) {
        $ext->unregister( $ed );
    }
}

sub resume {
    my ($self, $ed) = @_;
    foreach my $ext (values %$self) {
    $ext->register( $ed );
    }
}

sub finish {
    my ($self, $ed) = @_;
    foreach my $ext (values %$self) {
        $ext->finish;
        $ext->unregister( $ed );
    }
}

1;
