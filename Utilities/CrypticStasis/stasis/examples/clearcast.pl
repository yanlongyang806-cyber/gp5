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
use Stasis::EventDispatcher;
use POSIX ();

die "Usage: $0 logfile actor\n" unless @ARGV == 2;

# open the file
my ( $file, $actor ) = @ARGV;
my $parser = Stasis::Parser->new( compact => 1 );
open LOGFILE, $file or die "Could not open file: $file";

# interesting events
my @eaura = qw/SPELL_AURA_APPLIED SPELL_AURA_APPLIED_DOSE SPELL_AURA_REFRESH SPELL_AURA_REMOVED SPELL_AURA_REMOVED_DOSE/;
my @ecast = qw/SPELL_CAST_SUCCESS SPELL_MISSED SPELL_DAMAGE SPELL_HEAL/;

# isup - whether clearcasting is up
# aura - clearcasting aura events
# spells - all casts, healing, damage, and miss events.
my ( $isup, %aura, %spells ) = 0;
$aura{$_} = 0 foreach (@eaura);
$spells{$_} = {} foreach (@ecast);

# if we see clearcasting, do this
my $handler_aura = sub {
    my $e = shift;

    if( $e->{target_name} eq $actor && $e->{spellname} eq 'Clearcasting' ) {
        $isup = $e->{action} == Stasis::Parser::SPELL_AURA_REMOVED ? 0 : 1;
        
        $aura{ $parser->action_name( $e->{action} ) }++;
    }
};

# if we see a spellcast, do this
my $handler_spell = sub {
    my $e = shift;

    if( $e->{actor_name} eq $actor ) {
        $spells{ $parser->action_name( $e->{action} ) }{ $e->{spellid} }{name} ||= $e->{spellname};
        $spells{ $parser->action_name( $e->{action} ) }{ $e->{spellid} }{"count" . ($isup ? "_up" : "_down")}++;
        $spells{ $parser->action_name( $e->{action} ) }{ $e->{spellid} }{crit}++ if $e->{critical};
    }
};

my $ed = Stasis::EventDispatcher->new;
$ed->add( $handler_aura, @eaura );
$ed->add( $handler_spell, @ecast );

# empty progress bar
$| = 1;
my $ppos = 0;
pbar();

# line count
my $wcl;
$wcl++ while( <LOGFILE> );
seek LOGFILE, 0, 0;

# main loop
while( defined( my $line = <LOGFILE> ) ) {
    my $event = $parser->parse( $line );
    $ed->process( $event ) if $event->{action};

    # progress bar, 60 chars wide
    my $ppos_new = POSIX::ceil( ( $. - $wcl ) / $wcl * 58 );
    if( $ppos_new != $ppos ) {
        $ppos = $ppos_new;
        pbar();
    }
}

close LOGFILE;

# done reading file - write a report
print "\n\nCLEARCASTING\n";
while( my ( $kevent, $vevent ) = each (%aura) ) {
    my $name = $kevent;
    $name =~ s/SPELL_AURA_//;
    printf "    %-35s: %d\n", lc $name, $vevent;
}

while( my ( $kaction, $vaction ) = each( %spells ) ) {
    print "\n$kaction\n";
    
    while( my ( $kspell, $vspell) = each ( %$vaction) ) {
        no warnings;
        printf "    %-35s: %d (%d crit) (%d up, %d down, %0.1f%%)\n", $vspell->{name} . " ($kspell)", $vspell->{count_up} + $vspell->{count_down}, $vspell->{crit}, $vspell->{count_up}, $vspell->{count_down}, $vspell->{count_up} / ($vspell->{count_down}+$vspell->{count_up}) * 100;
    }
}

sub pbar {
    print "\r[", ( "=" x $ppos ), ( "-" x ( 58 - $ppos ) ), "] ", sprintf "%3d %%", ($ppos/58*100);
}
