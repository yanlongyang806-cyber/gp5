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

package Stasis::Page::Compare;

use strict;
use warnings;
use POSIX;
use HTML::Entities;
use Stasis::Parser;
use Stasis::Page;
use Stasis::PageMaker;
use Stasis::ActorGroup;
use Stasis::Extension qw/span_sum/;
use Stasis::MobUtil qw/splitguid/;

our @ISA = "Stasis::Page";

sub page {
    my ($self, $TYPE, @ARG) = @_;

    # Only can do classes.
    return if $TYPE ne "class" || @ARG == 0;
    
    # Gather actor list.
    my ($displayName, @actors) = ($ARG[0], $self->_class_setup($ARG[0]));
    my ($raidStart, $raidEnd, $raidPresence) = $self->{ext}{Presence}->presence;
    
    ################
    # INFO WE NEED #
    ################
    
    my $keyActor = sub { $self->{grouper}->captain_for($_[0]) };
    my @raiders = map { $self->{raid}{$_}{class} ? ( $_ ) : () } keys %{$self->{raid}};
    
    my $deOut = $self->{ext}{Damage}->sum( 
        actor => \@actors, 
        -target => \@raiders,
        expand => [ "spell", "actor" ],
        keyActor => $keyActor,
    );

    my $actOut = $self->{ext}{Activity}->sum(
        actor => \@actors,
        -target => \@raiders,
        expand => [ "actor" ],
        keyActor => $keyActor,
    );
    
    my $heOut = $self->{ext}{Healing}->sum( 
        actor => \@actors, 
        target => \@raiders,
        expand => [ "spell", "actor" ],
        keyActor => $keyActor,
    );
    
    # Presence and DPS time
    my (%presenceTime, %dpsTime);
    foreach my $actor (@actors) {
        my ($pstart, $pend, $ptime) = $self->{ext}{Presence}->presence($actor);
        
        $presenceTime{$actor} = $ptime;
        $dpsTime{$actor} = ref $actOut->{$actor}{spans} eq 'ARRAY' ? span_sum( $actOut->{$actor}{spans}, $pstart, $pend ) : 0;
    }
        
    # Healing totals
    while( my ($kspell, $vspell) = each(%$heOut) ) {
        while( my ($kactor, $vactor) = each(%$vspell) ) {
            $vactor->{count} = $self->_addHCT( $vactor, "Count" );
            $vactor->{total} = $self->_addHCT( $vactor, "Total" );
            $vactor->{effective} = $self->_addHCT( $vactor, "Effective" );
        }
    }
    
    ###############
    # PAGE HEADER #
    ###############
    
    # Print page header.
    my $PAGE;
    my $pm = $self->{pm};
    my $displayNameCSS = $displayName;
    $displayNameCSS =~ s/\s//g;
    
    my @tabs;
    # push @tabs, "Damage" if keys %$deOut;
    # push @tabs, "Healing" if keys %$heOut;
    
    $PAGE .= $pm->pageHeader( $self->{name}, $displayName );
    $PAGE .= $pm->statHeader( $self->{name}, $displayName, $raidStart );
    $PAGE .= sprintf "<h3><a class=\"color%s\" href=\"\">%ss</a></h3>", $displayNameCSS, $displayName;
    $PAGE .= $pm->tabBar( @tabs );
    
    ######################
    # DAMAGE AND HEALING #
    ######################
    
    my @actorsHeader = ( "Name", map { "R-" . $pm->actorLink($_) } @actors );
    
    if( keys %$deOut ) {
        # $PAGE .= $pm->tabStart("Damage");
        $PAGE .= $self->{pm}->tableStart;
        $PAGE .= $self->tableSpell( $deOut, "Damage Out", "total", \@actors, \@actorsHeader );
        # $PAGE .= $self->{pm}->tableEnd;
        # $PAGE .= $pm->tabEnd;
    }
    
    if( keys %$heOut ) {
        # $PAGE .= $pm->tabStart("Healing");
        $PAGE .= $self->{pm}->tableStart unless keys %$deOut;
        $PAGE .= $self->tableSpell( $heOut, "Healing Out", "effective", \@actors, \@actorsHeader );
        # $PAGE .= $self->{pm}->tableEnd;
        # $PAGE .= $pm->tabEnd;
    }

    # Print page footer.
    $PAGE .= $pm->tableEnd if keys %$heOut || keys %$deOut;
    $PAGE .= $pm->tabBarEnd;
    # $PAGE .= $pm->jsTab( $tabs[0] ) if @tabs;
    $PAGE .= $pm->pageFooter;
}

sub tableSpell {
    my ($self, $ext, $name, $what, $actors, $header) = @_;
    
    my $PAGE = "";
    
    # Compute totals
    my %spell_total;
    while( my ($kspell, $vspell) = each(%$ext) ) {
        while( my ($kactor, $vactor) = each(%$vspell) ) {
            $vactor->{total} = $self->_addHCT( $vactor, "Total" );
            $vactor->{count} = $self->_addHCT( $vactor, "Count" );
            $vactor->{effective} = $self->_addHCT( $vactor, "Effective" );
            $spell_total{$kspell} += $vactor->{ $what };
        }
    }
    
    # Print table header
    $PAGE .= $self->{pm}->tableHeader( $name, @$header );
    
    # Print rows
    foreach my $kspell (sort { $spell_total{$b} <=> $spell_total{$a} } grep { $spell_total{$_} } keys %$ext) {
        # Start off the row with a spell link.
        my @row = ( "Name" => $self->{pm}->spellLink( $kspell ) );
        
        # Iterate over actors
        for( my $iactor = 0; $iactor < @$actors; $iactor++ ) {
            push @row, $header->[$iactor+1] => $ext->{$kspell}{ $actors->[$iactor] }{ $what };
        }
        
        # Print the row
        $PAGE .= $self->{pm}->tableRow( header => $header, data => { @row }, );
    }
    
    return $PAGE;
}

sub _class_setup {
    my ($self, $class) = @_;
    
    return grep { 
        scalar $self->{ext}{Presence}->presence($_) > 0
    } grep { 
        $self->{raid}{$_}{class} && $self->{raid}{$_}{class} eq $class
    } keys %{$self->{raid}};
}

1;
