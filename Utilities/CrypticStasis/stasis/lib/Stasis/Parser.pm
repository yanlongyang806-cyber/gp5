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

package Stasis::Parser;

=head1 NAME

Stasis::Parser - parse a log file into a list of combat actions.

=head1 SYNOPSIS

    use Stasis::Parser;
    
    my $parser = Stasis::Parser->new( version => 2, year => 2008 );
    while( <STDIN> ) {
        my $event = $parser->parse( $_ );
        
        # whatever
    }

=head1 METHODS

=cut

use strict;
use warnings;
use POSIX;
use Carp;

use Stasis::Event qw/%action_map/;
use Stasis::Parser::V1;
use Stasis::Parser::V2;
use Stasis::Parser::Cryptic;

=head3 new

Takes three parameters.

=over 4

=item logger

The name of the logger. This value defaults to "You". The name of 
the logger is not required for version 2 logs (since they contain
the logger's real name).

=item version

"1" or "2" for pre-2.4 and post-2.4 logs respectively. The version
defaults to 2.

=item year

This optional argument can be used to specify a different year. The
year defaults to the current year.

=back

=head3 EXAMPLE

    $parser = Stasis::Parser->new ( 
        logger => "Gian",
        version => 1,
        year => 2008,
    );

=cut

sub new {
    my $class = shift;
    my %params = @_;
    
    $params{year} ||= strftime "%Y", localtime;
    $params{logger} ||= "You";
    $params{version} = 2 if !$params{version};

    if( $class eq "Stasis::Parser" ) {
    	if($params{version} eq 'Cryptic')
        {
            $class = "Stasis::Parser::Cryptic";
        }
        else
        {
            $class = $params{version} == 1 ? "Stasis::Parser::V1" : "Stasis::Parser::V2";
        }
    }
    
    bless {
        year => $params{year} || strftime( "%Y", localtime ),
        logger => $params{logger} || "You",
    }, $class;
}

1;
