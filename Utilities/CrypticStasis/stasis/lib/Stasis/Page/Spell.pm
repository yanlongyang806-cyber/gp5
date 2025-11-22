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

package Stasis::Page::Spell;

use strict;
use warnings;
use POSIX;
use HTML::Entities;
use Stasis::Page;
use Stasis::PageMaker;
use Stasis::ActorGroup;
use Stasis::Extension qw/span_sum/;

our @ISA = "Stasis::Page";

sub page {
    my $self = shift;
    my $SPELL = shift;
    
    return unless defined $SPELL;
    
    my $PAGE;
    my $pm = $self->{pm};
    
    ################
    # INFO WE NEED #
    ################
    
    my $keyActor = sub { $self->{grouper}->captain_for($_[0]) };
    
    my $deOut = $self->{ext}{Damage}->sum( 
        spell => [ $SPELL ], 
        expand => [ "actor", "target" ],
        keyActor => $keyActor,
    );

    my $heOut = $self->{ext}{Healing}->sum( 
        spell => [ $SPELL ], 
        expand => [ "actor", "target" ],
        keyActor => $keyActor,
    );
    
    my $castsOut = $self->{ext}{Cast}->sum( 
        spell => [ $SPELL ], 
        expand => [ "actor", "target" ],
        keyActor => $keyActor,
    );

    my $powerIn = $self->{ext}{Power}->sum( 
        spell => [ $SPELL ], 
        expand => [ "target", "actor" ],
        keyActor => $keyActor,
    );

    my $eaIn = $self->{ext}{ExtraAttack}->sum( 
        spell => [ $SPELL ], 
        expand => [ "target", "actor" ],
        keyActor => $keyActor,
    );
    
    my $auraIn = $self->{ext}{Aura}->sum( 
        spell => [ $SPELL ], 
        expand => [ "target", "actor" ],
        keyActor => $keyActor,
    );
    
    my $dispelOut = $self->{ext}{Dispel}->sum( 
        extraspell => [ $SPELL ], 
        expand => [ "actor", "target" ],
        keyActor => $keyActor,
    );
    
    my $interruptOut = $self->{ext}{Interrupt}->sum( 
        extraspell => [ $SPELL ], 
        expand => [ "actor", "target" ],
        keyActor => $keyActor,
    );
    
    ###############
    # PAGE HEADER #
    ###############
    
    my $displayName = HTML::Entities::encode_entities($self->{index}->spellname($SPELL)) || "Spell";
    my ($raidStart, $raidEnd, $raidPresence) = $self->{ext}{Presence}->presence();
    $PAGE .= $pm->pageHeader($self->{name}, $displayName);
    $PAGE .= $pm->statHeader($self->{name}, $displayName, $raidStart);
    $PAGE .= sprintf "<h3 class=\"colorMob\">%s</h3>", $pm->spellLink( $SPELL );
    
    my @summaryRows;
    
    # Wowhead link
    if( $SPELL =~ /^\d+$/ ) {
        push @summaryRows, "Wowhead Link" => sprintf "<a href=\"http://www.wowhead.com/?spell=%s\" target=\"swswh_%s\">%s</a> &#187;", $SPELL, $SPELL, $displayName;
    }
    
    # Other spells with the same name
    if( $SPELL ) {
        my @nameShare = $self->{index}->spellid( $SPELL );
        if( @nameShare > 1 ) {
            push @summaryRows, "Shares Name With" => join "<br />", map { $pm->spellLink($_) . ( $_ == $SPELL ? " (currently viewing)" : "" ) } @nameShare;
        }
    }
    
    $PAGE .= $pm->vertBox( "Spell summary", @summaryRows );
    $PAGE .= "<br />";
    
    my @tabs;
    push @tabs, "Damage" if %$deOut;
    push @tabs, "Healing" if %$heOut;
    push @tabs, "Auras" if %$auraIn;
    push @tabs, "Casts and Power" if %$castsOut || %$powerIn || %$eaIn;
    push @tabs, "Dispels and Interrupts" if %$dispelOut || %$interruptOut;
    
    $PAGE .= $pm->tabBar( @tabs );
    
    ##########
    # DAMAGE #
    ##########
    
    if( %$deOut ) {
        $PAGE .= $pm->tabStart("Damage");
        $PAGE .= $pm->tableStart;
        
        $PAGE .= $pm->tableRows(
            title => "Damage" . ( $_ ? " In by Target" : " Out by Source" ),
            header => [ ( $_ ? "Target" : "Source" ), "R-Total", "R-Hits", "R-Crits", "R-Ticks", "R-AvHit", "R-AvCrit", "R-AvTick", "R-% Crit", "R-Avoid", ],
            preprocess => sub {
                $_[1]->{total} = $self->_addHCT( $_[1], "Total" );
            },
            data => ( $_ ? $self->_flipRows($deOut) : $deOut ),
            sort => sub ($$) { ($_[1]->{total}||0) <=> ($_[0]->{total}||0) },
            master => sub {
                return $self->_rowDamage( $_[1], undef, ( $_ ? "Target" : "Source" ), $pm->actorLink( $_[0] ) );
            },
        ) foreach (0..1);
        
        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    ###########
    # HEALING #
    ###########
    
    if( %$heOut ) {
        $PAGE .= $pm->tabStart("Healing");
        $PAGE .= $pm->tableStart;
        
        $PAGE .= $pm->tableRows(
            title => "Healing" . ( $_ ? " In by Target" : " Out by Source" ),
            header => [ ( $_ ? "Target" : "Source" ), "R-Eff. Heal", "R-Hits", "R-Crits", "R-Ticks", "R-AvHit", "R-AvCrit", "R-AvTick", "R-% Crit", "R-Overheal", ],
            preprocess => sub {
                $_[1]->{count} = $self->_addHCT( $_[1], "Count" );
                $_[1]->{total} = $self->_addHCT( $_[1], "Total" );
                $_[1]->{effective} = $self->_addHCT( $_[1], "Effective" );
            },
            data => ( $_ ? $self->_flipRows($heOut) : $heOut ),
            sort => sub ($$) { ($_[1]->{effective}||0) <=> ($_[0]->{effective}||0) },
            master => sub {
                return $self->_rowHealing( $_[1], undef, ( $_ ? "Target" : "Source" ), $pm->actorLink( $_[0] ) );
            },
        ) foreach (0..1);
        
        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    ###################
    # CASTS AND POWER #
    ###################
    
    if( %$castsOut || %$powerIn || %$eaIn ) {
        $PAGE .= $pm->tabStart("Casts and Power");
        $PAGE .= $pm->tableStart;
        
        #########
        # CASTS #
        #########
        
        if( %$castsOut ) {
            $PAGE .= $pm->tableRows(
                title => "Casts" . ( $_ ? " In by Target" : " Out by Source" ),
                header => [ ( $_ ? "Target" : "Source" ), "R-Casts", "", "", "", ],
                data => ( $_ ? $self->_flipRows($castsOut) : $castsOut ),
                sort => sub ($$) { ($_[1]->{count}||0) <=> ($_[0]->{count}||0) },
                master => sub {
                    return {
                        ( $_ ? "Target" : "Source" ) => $pm->actorLink( $_[0] ),
                        "R-Casts" => $_[1]->{count},
                    }
                },
            ) foreach (0..1);
        }
        
        if( %$powerIn ) {
            $PAGE .= $pm->tableRows(
                title => "Power Gains",
                header => [ "Name", "R-Gained", "R-Ticks", "R-Avg", "R-Per 5", ],
                data => $powerIn,
                sort => sub ($$) { ($_[1]->{amount}||0) <=> ($_[0]->{amount}||0) },
                master => sub {
                    my $ptime = $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[0]) );
                    return {
                        "Name" => $pm->actorLink( $_[0] ),
                        "R-Gained" => $_[1]->{amount},
                        "R-Ticks" => $_[1]->{count},
                        "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                        "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                    }
                },
                slave => sub {
                    my $ptime = $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[2]) );
                    return {
                        "Name" => $pm->actorLink( $_[0] ),
                        "R-Gained" => $_[1]->{amount},
                        "R-Ticks" => $_[1]->{count},
                        "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                        "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                    }
                },
            );
        }
        
        if( %$eaIn ) {
            $PAGE .= $pm->tableRows(
                title => %$powerIn ? "" : "Power Gains",
                header => [ "Name", "R-Gained", "R-Ticks", "R-Avg", "R-Per 5", ],
                data => $eaIn,
                sort => sub ($$) { ($_[1]->{amount}||0) <=> ($_[0]->{amount}||0) },
                master => sub {
                    my $ptime = $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[0]) );
                    return {
                        "Name" => $pm->actorLink( $_[0] ),
                        "R-Gained" => $_[1]->{amount},
                        "R-Ticks" => $_[1]->{count},
                        "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                        "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                    }
                },
                slave => sub {
                    my $ptime = $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[2]) );
                    return {
                        "Name" => $pm->actorLink( $_[0] ),
                        "R-Gained" => $_[1]->{amount},
                        "R-Ticks" => $_[1]->{count},
                        "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                        "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                    }
                },
            );
        }
        
        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    ##########################
    # DISPELS and INTERRUPTS #
    ##########################
    
    if( %$dispelOut || %$interruptOut ) {
        $PAGE .= $pm->tabStart("Dispels and Interrupts");
        $PAGE .= $pm->tableStart;
        
        ###########
        # DISPELS #
        ###########
        
        if( %$dispelOut ) {
            $PAGE .= $pm->tableRows(
                title => "Dispelled" . ( $_ ? " on Target" : " by Source" ),
                header => [ ( $_ ? "Target" : "Source" ), "R-Casts", "R-Resists" ],
                data => ( $_ ? $self->_flipRows($dispelOut) : $dispelOut ),
                sort => sub ($$) { ($_[1]->{count}||0) <=> ($_[0]->{count}||0) },
                master => sub {
                    return {
                        ( $_ ? "Target" : "Source" ) => $pm->actorLink( $_[0] ),
                        "R-Casts" => $_[1]->{count},
                        "R-Resists" => $_[1]->{resist},
                    }
                },
            ) foreach (0..1);
        }
        
        if( %$interruptOut ) {
            $PAGE .= $pm->tableRows(
                title => "Interrupts " . ( $_ ? " on Casters" : " by Interrupter" ),
                header => [ ( $_ ? "Target" : "Source" ), "R-Casts", "R-Resists" ],
                data => ( $_ ? $self->_flipRows($interruptOut) : $interruptOut ),
                sort => sub ($$) { ($_[1]->{count}||0) <=> ($_[0]->{count}||0) },
                master => sub {
                    return {
                        ( $_ ? "Target" : "Source" ) => $pm->actorLink( $_[0] ),
                        "R-Casts" => $_[1]->{count},
                        "R-Resists" => $_[1]->{resist},
                    }
                },
            ) foreach (0..1);
        }
        
        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    #########
    # AURAS #
    #########
    
    if( %$auraIn ) {
        $PAGE .= $pm->tabStart("Auras");
        $PAGE .= $pm->tableStart;
        
        $PAGE .= $pm->tableRows(
            title => "Auras Gained",
            header => [ "Name", "Type", "R-Uptime", "R-%", "R-Gained", "R-Faded", ],
            data => $auraIn,
            preprocess => sub { 
                if( @_ == 4 ) {
                    # Slave row
                    $_[1]->{time} = span_sum( $_[1]->{spans}, $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[2]) ) );
                } else {
                    # Master row.
                    $_[1]->{time} = span_sum( $_[1]->{spans}, $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[0]) ) );
                }
            },
            sort => sub ($$) { $_[0]->{type} cmp $_[1]->{type} || $_[1]->{time} <=> $_[0]->{time} },
            master => sub {
                my $ptime = $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[0]) );
                my ($gains, $fades);
                foreach( @{$_[1]->{spans}} ) {
                    my ($start, $end) = unpack "dd", $_;
                    $gains++ if $start;
                    $fades++ if $end;
                }
                
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "Type" => (($_[1]->{type} && lc $_[1]->{type}) || "unknown"),
                    "R-Gained" => $gains,
                    "R-Faded" => $fades,
                    "R-%" => $ptime && sprintf( "%0.1f%%", $_[1]->{time} / $ptime * 100 ),
                    "R-Uptime" => $_[1]->{time} && sprintf( "%02d:%02d", $_[1]->{time}/60, $_[1]->{time}%60 ),
                };
            },
            slave => sub {
                my $ptime = $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[2]) );
                my ($gains, $fades);
                foreach( @{$_[1]->{spans}} ) {
                    my ($start, $end) = unpack "dd", $_;
                    $gains++ if $start;
                    $fades++ if $end;
                }
                
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "Type" => (($_[1]->{type} && lc $_[1]->{type}) || "unknown"),
                    "R-Gained" => $gains,
                    "R-Faded" => $fades,
                    "R-%" => $ptime && sprintf( "%0.1f%%", $_[1]->{time} / $ptime * 100 ),
                    "R-Uptime" => $_[1]->{time} && sprintf( "%02d:%02d", $_[1]->{time}/60, $_[1]->{time}%60 ),
                };
            }
        );
        
        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    $PAGE .= $pm->jsTab($tabs[0]) if @tabs;
    $PAGE .= $pm->tabBarEnd;
    
    $PAGE .= $pm->pageFooter;
    
    return $PAGE;
}

sub _flipRows {
    my ($self, $eOut) = @_;
    
    # We want to make this a two-dimensional target + actor hash, instead of actor + target
    my %ret;
    
    while( my ($kactor, $vactor) = each(%$eOut) ) {
        while( my ($ktarget, $vtarget) = each(%$vactor) ) {
            # Add a reference to this leaf.
            $ret{ $ktarget }{ $kactor } = $vtarget;
        }
    }
    
    return \%ret;
}

1;
