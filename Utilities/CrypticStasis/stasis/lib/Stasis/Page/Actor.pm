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

package Stasis::Page::Actor;

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
    my ($self, $MOB, $do_group) = @_;
    my $MOB_GROUP = $self->{grouper}->group($MOB);
    my @PLAYER = $do_group && $MOB_GROUP ? @{ $MOB_GROUP->{members} } : ($MOB);
    return unless @PLAYER;
    
    my $PAGE;
    my $pm = $self->{pm};
    
    #################
    # RAID DURATION #
    #################
    
    # Determine start time and end times (earliest/latest presence)
    my ($raidStart, $raidEnd, $raidPresence) = $self->{ext}{Presence}->presence();
    my ($pstart, $pend, $ptime) = $self->{ext}{Presence}->presence( @PLAYER );
    
    # Player and pets
    my @playpet = ( @PLAYER );
    foreach my $player (@PLAYER) {
        # Add pets.
        push @playpet, @{$self->{raid}{$player}{pets}} 
            if( exists $self->{raid}{$player} && exists $self->{raid}{$player}{pets} );
    }
    
    # Are we a raider?
    my $is_raider = exists $self->{raid}{$MOB} && $self->{raid}{$MOB}{class};
    
    ################
    # INFO WE NEED #
    ################
    
    my $keyActor = sub { $self->{grouper}->captain_for($_[0]) };
    my $keyActorWithPets;
    
    {
        # Used for grouping pets with their owners for damage and healing in.
        # Doing it this way means we can't use encoded spell IDs to show the name of the pet, maybe change this.
        
        my %pet_owners;
        foreach my $player (keys %{$self->{raid}}) {
            if( $self->{raid}{$player}{class} && $self->{raid}{$player}{pets} && $self->{raid}{$player}{class} ne "Pet" ) {
                $pet_owners{$_} = $player foreach @{$self->{raid}{$player}{pets}};
            }
        }
        
        $keyActorWithPets = sub { $self->{grouper}->captain_for($pet_owners{$_[0]} || $_[0]) };
    }
    
    my $actOut = $self->{ext}{Activity}->sum(
        actor => \@playpet,
        expand => [ "target" ],
        keyActor => $keyActor,
    );
    
    my $actIn = $self->{ext}{Activity}->sum(
        target => \@playpet,
        expand => [ "actor" ],
        keyActor => $keyActor,
    );
    
    # For raiders, dpsTime is against enemies!
    my ($dpsStart, $dpsEnd, $dpsTime) = span_sum( [ map { $actOut->{$_}{spans} ? @{$actOut->{$_}{spans}} : () } grep { !$is_raider || ( !exists $self->{raid}{$_} || !$self->{raid}{$_}{class} ) } keys %$actOut ], $pstart, $pend );
    
    my $deOut = $self->{ext}{Damage}->sum( 
        actor => \@playpet, 
        expand => [ "actor", "spell", "target" ],
        keyActor => $keyActor,
    );

    my $deIn = $self->{ext}{Damage}->sum( 
        target => \@PLAYER, 
        expand => [ "actor", "spell" ],
        keyActor => $keyActorWithPets,
    );
    
    my $heOut = $self->{ext}{Healing}->sum( 
        actor => \@playpet, 
        expand => [ "actor", "spell", "target" ],
        keyActor => $keyActor,
    );
    
    my $heIn = $self->{ext}{Healing}->sum( 
        target => \@PLAYER, 
        expand => [ "actor", "spell" ],
        keyActor => $keyActorWithPets,
    );
    
    my $castsOut = $self->{ext}{Cast}->sum( 
        actor => \@PLAYER, 
        expand => [ "spell", "target" ], 
        keyActor => $keyActor,
    );

    my $powerIn = $self->{ext}{Power}->sum( 
        target => \@PLAYER, 
        expand => [ "spell", "actor" ], 
        keyActor => $keyActor,
    );
    
    my $powerOut = $self->{ext}{Power}->sum( 
        actor => \@PLAYER, 
        -target => \@PLAYER,
        expand => [ "spell", "target" ], 
        keyActor => $keyActor,
    );

    my $eaIn = $self->{ext}{ExtraAttack}->sum( 
        target => \@PLAYER, 
        expand => [ "spell", "actor" ], 
        keyActor => $keyActor,
    );
    
    my $interruptOut = $self->{ext}{Interrupt}->sum( 
        actor => \@PLAYER, 
        expand => [ "extraspell", "target" ], 
        keyActor => $keyActor,
    );
    
    my $dispelOut = $self->{ext}{Dispel}->sum( 
        actor => \@PLAYER, 
        expand => [ "extraspell", "target" ], 
        keyActor => $keyActor,
    );
    
    my $interruptIn = $self->{ext}{Interrupt}->sum( 
        target => \@PLAYER, 
        expand => [ "extraspell", "actor" ], 
        keyActor => $keyActor,
    );
    
    my $dispelIn = $self->{ext}{Dispel}->sum( 
        target => \@PLAYER, 
        expand => [ "extraspell", "actor" ], 
        keyActor => $keyActor,
    );
    
    my ($auraIn, $auraOut);
    if( ! $do_group ) {
        $auraIn = $self->{ext}{Aura}->sum( 
            target => [ $MOB ], 
            expand => [ "spell", "actor" ], 
            keyActor => $keyActor,
        );

        $auraOut = $self->{ext}{Aura}->sum( 
            actor => [ $MOB ], 
            -target => [ $MOB ],
            expand => [ "spell", "target" ], 
            keyActor => $keyActor,
        );
    }
    
    ###############
    # DAMAGE SUMS #
    ###############
    
    # Total damage, and damage from/to mobs
    my $dmg_from_all = 0;
    my $dmg_from_mobs = 0;
    
    my $dmg_to_all = 0;
    my $dmg_to_mobs = 0;
    
    while( my ($kactor, $vactor) = each(%$deOut) ) {
        while( my ($kspell, $vspell) = each(%$vactor) ) {
            while( my ($ktarget, $vtarget) = each(%$vspell) ) {
                $vtarget->{total} = $self->_addHCT( $vtarget, "Total" );
                
                $dmg_to_all += $vtarget->{total} || 0;
                $dmg_to_mobs += $vtarget->{total} || 0 if !$self->{raid}{$ktarget} || !$self->{raid}{$ktarget}{class};
            }
        }
    }
    
    while( my ($kactor, $vactor) = each(%$deIn) ) {
        while( my ($kspell, $vspell) = each(%$vactor) ) {            
            $vspell->{total} = $self->_addHCT( $vspell, "Total" );
            
            $dmg_from_all += $vspell->{total} || 0;
            $dmg_from_mobs += $vspell->{total} || 0 if !$self->{raid}{$kactor} || !$self->{raid}{$kactor}{class};
        }
    }
    
    ###############
    # PAGE HEADER #
    ###############
    
    my $displayName = sprintf "%s%s", HTML::Entities::encode_entities($self->{index}->actorname($MOB)), @PLAYER > 1 ? " (group)" : "";
    $displayName ||= "Actor";
    $PAGE .= $pm->pageHeader($self->{name}, $displayName);
    $PAGE .= $pm->statHeader($self->{name}, $displayName, $raidStart);
    $PAGE .= sprintf "<h3 class=\"color%s\">%s%s</h3>", $self->{raid}{$MOB}{class} || "Mob", $pm->actorLink($MOB, @PLAYER == 1 ? 1 : 0 ), @PLAYER > 1 ? " (group)" : "";
    
    my @summaryRows;
    
    # Type info
    push @summaryRows, "Class" => $self->{raid}{$MOB}{class} || "Mob";
    
    # Wowhead link
    if( $MOB && ! $self->{raid}{$MOB}{class} ) {
        my (undef, $npc, undef) = splitguid $MOB;
        push @summaryRows, "Wowhead Link" => sprintf "<a href=\"http://www.wowhead.com/?npc=%s\" target=\"swswha_%s\">%s (#%s)</a> &#187;", $npc, $npc, HTML::Entities::encode_entities($self->{index}->actorname($MOB)), $npc if $npc && $npc < 2**16;
    }
    
    if( $self->{server} && $self->{raid}{$MOB}{class} && $self->{raid}{$MOB}{class} ne "Pet" ) {
        my $r = $self->{server};
        my $n = $self->{index}->actorname($MOB);
        $r =~ s/([^A-Za-z0-9])/sprintf("%%%02X", ord($1))/seg;
        $n =~ s/([^A-Za-z0-9])/sprintf("%%%02X", ord($1))/seg;
        push @summaryRows, "Armory" => "<a href=\"http://www.wowarmory.com/character-sheet.xml?r=$r&n=$n\" target=\"swsar_$n\">$displayName &#187;</a>";
    }
    
    # Presence
    push @summaryRows, "Presence" => $pm->timespan( $pstart, $pend, $raidStart, undef, 1 );
    
    # Activity
    push @summaryRows, "DPS activity" => $pm->timespan( $dpsStart, $dpsEnd, $raidStart, $dpsTime, 1 );
    
    # Owner info
    if( $self->{raid}{$MOB} && $self->{raid}{$MOB}{class} && $self->{raid}{$MOB}{class} eq "Pet" ) {
        foreach my $raider (keys %{$self->{raid}}) {
            if( grep $_ eq $MOB, @{$self->{raid}{$raider}{pets}}) {
                push @summaryRows, "Owner" => $pm->actorLink($raider);
                last;
            }
        }
    }
    
    # Pet info
    {
        my %pets;
        foreach my $p (@PLAYER) {
            if( exists $self->{raid}{$p} && exists $self->{raid}{$p}{pets} ) {
                $pets{ $keyActor->($_) } = 1 foreach ( grep { $self->{ext}{Presence}->presence($_) } @{$self->{raid}{$p}{pets}} );
            }
        }
        
        if( %pets ) {
            push @summaryRows, "Pets" => join "<br />", map { $pm->actorLink($_) } sort { $self->{index}->actorname($a) cmp $self->{index}->actorname($b) } keys %pets;
        }
    }
    
    # Damage Info
    if( $dmg_to_all ) {
        my $dmg_to_all_extra = $is_raider ? ( $dmg_to_all - $dmg_to_mobs ? " (" . ($dmg_to_all - $dmg_to_mobs) . " was to players)" : "" ) : "";
        my $dmg_in_extra = $is_raider ? ( $dmg_from_all - $dmg_from_mobs ? " (" . ($dmg_from_all - $dmg_from_mobs) . " was from players)" : "" ) : "";
        
        push @summaryRows, (
            "Damage in" => $dmg_from_all . $dmg_in_extra,
            "Damage out" => $dmg_to_all . $dmg_to_all_extra,
        );
    }
    
    # DPS
    if( $ptime && $dpsTime ) {
        my $dps_damage = $is_raider ? $dmg_to_mobs : $dmg_to_all;
        
        push @summaryRows, (
            "DPS (over presence)" => sprintf( "%d", $dps_damage/$ptime ),
            "DPS (over activity)" => sprintf( "%d", $dps_damage/$dpsTime ),
        );
    }
    
    $PAGE .= $pm->vertBox( "Actor summary", @summaryRows );
    $PAGE .= "<br />";
    
    if( $MOB_GROUP ) {
        # Group information
        my $group_text = "<div align=\"left\">This is a group composed of " . @{$MOB_GROUP->{members}} . " mobs.<br />";
        
        $group_text .= "<br /><b>Group Link</b><br />";
        $group_text .= sprintf "%s%s<br />", $pm->actorLink($MOB), ( $do_group ? " (currently viewing)" : "" );
        
        $group_text .= "<br /><b>Member Links</b><br />";

        if( $self->{collapse} ) {
            $group_text .= "Member links are disabled for this group."
        } else {
            foreach (@{$MOB_GROUP->{members}}) {
                $group_text .= sprintf "%s%s<br />", $pm->actorLink($_, 1), ( !$do_group && $_ eq $MOB ? " (currently viewing)" : "" );
            }
        }
        
        $group_text .= "</div>";
        
        $PAGE .= $pm->textBox( $group_text, "Group Information" );
        $PAGE .= "<br />";
    }
    
    my @tabs;
    push @tabs, "Damage" if %$deOut || %$deIn;
    push @tabs, "Healing" if %$heOut || %$heIn;
    push @tabs, "Auras" if ($auraIn && %$auraIn) || ($auraOut && %$auraOut);
    push @tabs, "Casts and Power" if %$castsOut || %$powerIn || %$eaIn || %$powerOut;
    push @tabs, "Dispels and Interrupts" if %$interruptIn || %$interruptOut || %$dispelIn || %$dispelOut;
    push @tabs, "Deaths" if (!$do_group || !$self->{collapse} ) && $self->_keyExists( $self->{ext}{Death}{actors}, @PLAYER );
    
    $PAGE .= $pm->tabBar(@tabs);
    
    ##########
    # DAMAGE #
    ##########
    
    my $deOutAbility;
    if( %$deOut || %$deIn ) {
        $PAGE .= $pm->tabStart("Damage");
        $PAGE .= $pm->tableStart;
        
        ########################
        # DAMAGE OUT ABILITIES #
        ########################
        
        $deOutAbility = $self->_abilityRows($deOut);
        
        $PAGE .= $pm->tableRows(
            title => "Damage Out by Ability",
            header => [ "Ability", "R-Total", "R-%", "", "", "R-Direct", "R-Ticks", "R-AvHit", "R-AvCrit", "R-AvTick", "R-% Crit", "R-Avoid", ],
            data => $deOutAbility,
            sort => sub ($$) { ($_[1]->{total}||0) <=> ($_[0]->{total}||0) },
            master => sub {
                my ($spellactor, $spellname, $spellid) = $self->_decodespell($_[0], $pm, "Damage", @PLAYER);
                return $self->_rowDamage( $_[1], $dmg_to_all, "Ability", $spellname );
            },
            slave => sub {
                return $self->_rowDamage( $_[1], $_[3]->{total}, "Ability", $pm->actorLink( $_[0] ) );
            }
        ) if %$deOut;

        ######################
        # DAMAGE OUT TARGETS #
        ######################
        
        $PAGE .= $pm->tableRows(
            title => "Damage Out by Target",
            header => [ "Target", "R-Total", "R-%", "R-DPS", "R-Time", "R-Direct", "R-Ticks", "R-AvHit", "R-AvCrit", "R-AvTick", "R-% Crit", "R-Avoid", ],
            data => $self->_targetRows($deOut),
            sort => sub ($$) { ($_[1]->{total}||0) <=> ($_[0]->{total}||0) },
            master => sub {
                my $dpsTime = $actOut->{$_[0]} && $actOut->{$_[0]}{spans} && span_sum($actOut->{$_[0]}{spans});
                return $self->_rowDamage( $_[1], $dmg_to_all, "Target", $pm->actorLink( $_[0] ), $dpsTime );
            },
            slave => sub {
                my ($spellactor, $spellname, $spellid) = $self->_decodespell($_[0], $pm, "Damage", @PLAYER);
                return $self->_rowDamage( $_[1], $_[3]->{total}, "Target", $spellname );
            }
        ) if %$deOut;

        #####################
        # DAMAGE IN SOURCES #
        #####################

        $PAGE .= $pm->tableRows(
            title => "Damage In by Source",
            header => [ "Source", "R-Total", "R-%", "R-DPS", "R-Time", "R-Direct", "R-Ticks", "R-AvHit", "R-AvCrit", "R-AvTick", "R-% Crit", "R-Avoid", ],
            data => $deIn,
            sort => sub ($$) { ($_[1]->{total}||0) <=> ($_[0]->{total}||0) },
            master => sub {
                my $dpsTime = $actIn->{$_[0]} && $actIn->{$_[0]}{spans} && span_sum($actIn->{$_[0]}{spans});
                return $self->_rowDamage( $_[1], $dmg_from_all, "Source", $pm->actorLink( $_[0] ), $dpsTime );
            },
            slave => sub {
                return $self->_rowDamage( $_[1], $_[3]->{total}, "Source", $pm->spellLink( $_[0], "Damage" ) );
            }
        ) if %$deIn;
        
        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    ###########
    # HEALING #
    ###########
    
    my ($eff_on_others, $eff_on_me, $heOutAbility);
    if( %$heIn || %$heOut ) {
        $PAGE .= $pm->tabStart("Healing");
        $PAGE .= $pm->tableStart;
        
        #########################
        # HEALING OUT ABILITIES #
        #########################
        
        $heOutAbility = $self->_abilityRows($heOut);
        
        $PAGE .= $pm->tableRows(
            title => "Healing Out by Ability",
            header => [ "Ability", "R-Eff. Heal", "R-%", "R-Direct", "R-Ticks", "R-AvHit", "R-AvCrit", "R-AvTick", "R-% Crit", "R-Overheal", ],
            data => $heOutAbility,
            sort => sub ($$) { ($_[1]->{effective}||0) <=> ($_[0]->{effective}||0) },
            preprocess => sub { 
                # Sum on all rows
                $_[1]->{count} = $self->_addHCT( $_[1], "Count" );
                $_[1]->{total} = $self->_addHCT( $_[1], "Total" );
                $_[1]->{effective} = $self->_addHCT( $_[1], "Effective" );
                
                # Add to eff_on_others if this is a slave row
                $eff_on_others += ($_[1]->{effective}||0) if( @_ == 2 );
            },
            master => sub {
                my ($spellactor, $spellname, $spellid) = $self->_decodespell($_[0], $pm, "Healing", @PLAYER);
                return $self->_rowHealing( $_[1], $eff_on_others, "Ability", $spellname );
            },
            slave => sub {
                return $self->_rowHealing( $_[1], $_[3]->{effective}, "Ability", $pm->actorLink( $_[0] ) );
            }
        ) if %$heOut;

        #######################
        # HEALING OUT TARGETS #
        #######################
        
        $PAGE .= $pm->tableRows(
            title => "Healing Out by Target",
            header => [ "Target", "R-Eff. Heal", "R-%", "R-Direct", "R-Ticks", "R-AvHit", "R-AvCrit", "R-AvTick", "R-% Crit", "R-Overheal", ],
            data => $self->_targetRows($heOut),
            sort => sub ($$) { ($_[1]->{effective}||0) <=> ($_[0]->{effective}||0) },
            preprocess => sub { 
                # Sum on all rows
                $_[1]->{count} = $self->_addHCT( $_[1], "Count" );
                $_[1]->{total} = $self->_addHCT( $_[1], "Total" );
                $_[1]->{effective} = $self->_addHCT( $_[1], "Effective" );
            },
            master => sub {
                return $self->_rowHealing( $_[1], $eff_on_others, "Target", $pm->actorLink($_[0]) );
            },
            slave => sub {
                my ($spellactor, $spellname, $spellid) = $self->_decodespell($_[0], $pm, "Healing", @PLAYER);
                return $self->_rowHealing( $_[1], $_[3]->{effective}, "Target", $spellname );
            }
        ) if %$heOut;

        ######################
        # HEALING IN SOURCES #
        ######################

        $PAGE .= $pm->tableRows(
            title => "Healing In by Source",
            header => [ "Source", "R-Eff. Heal", "R-%", "R-Direct", "R-Ticks", "R-AvHit", "R-AvCrit", "R-AvTick", "R-% Crit", "R-Overheal", ],
            data => $heIn,
            sort => sub ($$) { ($_[1]->{effective}||0) <=> ($_[0]->{effective}||0) },
            preprocess => sub { 
                # Sum on all rows
                $_[1]->{count} = $self->_addHCT( $_[1], "Count" );
                $_[1]->{total} = $self->_addHCT( $_[1], "Total" );
                $_[1]->{effective} = $self->_addHCT( $_[1], "Effective" );
                
                # Add to eff_on_me if this is a slave row
                $eff_on_me += ($_[1]->{effective}||0) if( @_ == 2 );
            },
            master => sub {
                return $self->_rowHealing( $_[1], $eff_on_me, "Source", $pm->actorLink($_[0]) );
            },
            slave => sub {
                return $self->_rowHealing( $_[1], $_[3]->{effective}, "Source", $pm->spellLink($_[0], "Healing") );
            }
        ) if %$heIn;

        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    ###################
    # CASTS AND POWER #
    ###################
    
    if( %$castsOut || %$powerIn || %$eaIn || %$powerOut ) {
        $PAGE .= $pm->tabStart("Casts and Power");
        $PAGE .= $pm->tableStart;

        #########
        # CASTS #
        #########
        
        # Inject fake junk into castsOut that makes the page a little more useful.

        # foreach my $eOutAbility ($deOutAbility, $heOutAbility) {
        #     if( $eOutAbility ) {
        #         while( my ($kspell, $vspell) = each (%$eOutAbility) ) {
        #             # This might be an encoded spell.
        #             my ($spellactor, $spellname, $spellid) = $self->_decodespell($kspell, $pm, "Casts and Power", @PLAYER);
        #             if( ( !$castsOut->{$spellid} ) && grep $_ eq $spellactor, @PLAYER ) {
        #                 # Missing from Casts table, and this is not a pet action. Inject fake entries.
        #                 while( my ($ktarget, $vtarget) = each (%$vspell) ) {
        #                     $castsOut->{$spellid}{$ktarget} = {
        #                         count => ((($vtarget->{hitCount}||0) + ($vtarget->{critCount}||0))||0),
        #                         ticks => $vtarget->{tickCount},
        #                         fake => 1,
        #                     }
        #                 }
        #             }
        #         }
        #     }
        # }

        $PAGE .= $pm->tableRows(
            title => "Casts",
            header => [ "Name", "R-Targets", "R-Casts", "R-Ticks", "", "", ],
            data => $castsOut,
            sort => sub ($$) { ($_[1]->{count}||0) <=> ($_[0]->{count}||0) },
            master => sub {
                my $link = $pm->spellLink( $_[0], "Casts and Power" );
                $link = "<i>$link</i>" if $_[1]->{fake};
                
                return {
                    "Name" => $link,
                    "R-Targets" => scalar keys %{$castsOut->{$_[0]}},
                    "R-Casts" => $_[1]->{count},
                    "R-Ticks" => $_[1]->{ticks},
                };
            },
            slave => sub {
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "R-Targets" => "",
                    "R-Casts" => $_[1]->{count},
                    "R-Ticks" => $_[1]->{ticks},
                };
            },
        ) if %$castsOut;

        ###########################
        # POWER and EXTRA ATTACKS #
        ###########################

        $PAGE .= $pm->tableRows(
            title => "Power Gains",
            header => [ "Name", "R-Sources", "R-Gained", "R-Ticks", "R-Avg", "R-Per 5", ],
            data => $powerIn,
            sort => sub ($$) { $_[1]->{amount} <=> $_[0]->{amount} },
            master => sub {
                return {
                    "Name" => $pm->spellLink( $_[0], "Casts and Power" ) . " (" . Stasis::Event->powerName( $_[1]->{type} ) . ")",
                    "R-Sources" => scalar keys %{$powerIn->{$_[0]}},
                    "R-Gained" => $_[1]->{amount},
                    "R-Ticks" => $_[1]->{count},
                    "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                    "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                };
            },
            slave => sub {
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "R-Gained" => $_[1]->{amount},
                    "R-Ticks" => $_[1]->{count},
                    "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                    "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                };
            },
        ) if %$powerIn;

        $PAGE .= $pm->tableRows(
            title => %$powerIn ? "" : "Power Gains",
            header => [ "Name", "R-Sources", "R-Gained", "R-Ticks", "R-Avg", "R-Per 5", ],
            data => $eaIn,
            sort => sub ($$) { $_[1]->{amount} <=> $_[0]->{amount} },
            master => sub {
                return {
                    "Name" => $pm->spellLink( $_[0], "Casts and Power" ) . " (extra attacks)",
                    "R-Sources" => scalar keys %{$eaIn->{$_[0]}},
                    "R-Gained" => $_[1]->{amount},
                    "R-Ticks" => $_[1]->{count},
                    "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                    "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                };
            },
            slave => sub {
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "R-Gained" => $_[1]->{amount},
                    "R-Ticks" => $_[1]->{count},
                    "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                    "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                };
            },
        ) if %$eaIn;
        
        #############
        # POWER OUT #
        #############
        
        $PAGE .= $pm->tableRows(
            title => "Power Given to Others",
            header => [ "Name", "R-Targets", "R-Given", "R-Ticks", "R-Avg", "R-Per 5", ],
            data => $powerOut,
            sort => sub ($$) { $_[1]->{amount} <=> $_[0]->{amount} },
            master => sub {
                return {
                    "Name" => $pm->spellLink( $_[0], "Casts and Power" ) . " (" . Stasis::Event->powerName( $_[1]->{type} ) . ")",
                    "R-Targets" => scalar keys %{$powerOut->{$_[0]}},
                    "R-Given" => $_[1]->{amount},
                    "R-Ticks" => $_[1]->{count},
                    "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                    "R-Per 5" => $ptime && sprintf( "%0.1f", $_[1]->{amount} / $ptime * 5 ),
                };
            },
            slave => sub {
                my $slave_ptime = $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[0]) );
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "R-Given" => $_[1]->{amount},
                    "R-Ticks" => $_[1]->{count},
                    "R-Avg" => $_[1]->{count} && sprintf( "%d", $_[1]->{amount} / $_[1]->{count} ),
                    "R-Per 5" => $slave_ptime && sprintf( "%0.1f", $_[1]->{amount} / $slave_ptime * 5 ),
                };
            },
        ) if %$powerOut;
                
        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    #########
    # AURAS #
    #########

    if( ($auraIn && %$auraIn) || ($auraOut && %$auraOut) ) {
        $PAGE .= $pm->tabStart("Auras");
        $PAGE .= $pm->tableStart;

        if( !$do_group ) {
            $PAGE .= $pm->tableRows(
                title => "Auras Gained",
                header => [ "Name", "Type", "R-Uptime", "R-%", "R-Gained", "R-Faded", ],
                data => $auraIn,
                preprocess => sub { $_[1]->{time} = span_sum( $_[1]->{spans}, $pstart, $pend ) },
                sort => sub ($$) { $_[0]->{type} cmp $_[1]->{type} || $_[1]->{time} <=> $_[0]->{time} },
                master => sub {
                    my ($gains, $fades);
                    foreach( @{$_[1]->{spans}} ) {
                        my ($start, $end) = unpack "dd", $_;
                        $gains++ if $start;
                        $fades++ if $end;
                    }
                    
                    return {
                        "Name" => $pm->spellLink( $_[0], "Auras" ),
                        "Type" => (($_[1]->{type} && lc $_[1]->{type}) || "unknown"),
                        "R-Gained" => $gains,
                        "R-Faded" => $fades,
                        "R-%" => $ptime && sprintf( "%0.1f%%", $_[1]->{time} / $ptime * 100 ),
                        "R-Uptime" => $_[1]->{time} && sprintf( "%02d:%02d", $_[1]->{time}/60, $_[1]->{time}%60 ),
                    };
                },
                slave => sub {
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
            ) if %$auraIn;

            $PAGE .= $pm->tableRows(
                title => "Auras Applied to Others",
                header => [ "Name", "Type", "R-Uptime", "R-%", "R-Gained", "R-Faded", ],
                data => $auraOut,
                preprocess => sub {
                    if( @_ == 4 ) {
                        # Slave row
                        $_[1]->{time} = span_sum( $_[1]->{spans}, $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[0]) ) );
                    } else {
                        # Master row.
                        $_[1]->{time} = span_sum( $_[1]->{spans}, $pstart, $pend );
                    }
                },
                sort => sub ($$) { $_[0]->{type} cmp $_[1]->{type} || $_[1]->{time} <=> $_[0]->{time} },
                master => sub {
                    my ($gains, $fades);
                    foreach( @{$_[1]->{spans}} ) {
                        my ($start, $end) = unpack "dd", $_;
                        $gains++ if $start;
                        $fades++ if $end;
                    }
                    
                    return {
                        "Name" => $pm->spellLink( $_[0], "Auras" ),
                        "Type" => (($_[1]->{type} && lc $_[1]->{type}) || "unknown"),
                        "R-Gained" => $gains,
                        "R-Faded" => $fades,
                        "R-%" => $ptime && sprintf( "%0.1f%%", $_[1]->{time} / $ptime * 100 ),
                        "R-Uptime" => $_[1]->{time} && sprintf( "%02d:%02d", $_[1]->{time}/60, $_[1]->{time}%60 ),
                    };
                },
                slave => sub {
                    my $slave_ptime = $self->{ext}{Presence}->presence( $self->{grouper}->expand($_[0]) );
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
                        "R-%" => $slave_ptime && sprintf( "%0.1f%%", $_[1]->{time} / $slave_ptime * 100 ),
                        "R-Uptime" => $_[1]->{time} && sprintf( "%02d:%02d", $_[1]->{time}/60, $_[1]->{time}%60 ),
                    };
                }
            ) if %$auraOut;
        }

        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    ##########################
    # DISPELS AND INTERRUPTS #
    ##########################
    
    if( %$interruptIn || %$interruptOut || %$dispelIn || %$dispelOut ) {
        $PAGE .= $pm->tabStart("Dispels and Interrupts");
        $PAGE .= $pm->tableStart;

        ###############
        # DISPELS OUT #
        ###############

        $PAGE .= $pm->tableRows(
            title => "Auras Dispelled on Others",
            header => [ "Name", "R-Targets", "R-Casts", "R-Resists", ],
            data => $dispelOut,
            sort => sub ($$) { $_[1]->{count} <=> $_[0]->{count} },
            master => sub {
                return {
                    "Name" => $pm->spellLink( $_[0], "Dispels and Interrupts" ),
                    "R-Targets" => scalar keys %{$dispelOut->{$_[0]}},
                    "R-Casts" => $_[1]->{count},
                    "R-Resists" => $_[1]->{resist},
                };
            },
            slave => sub {
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "R-Casts" => $_[1]->{count},
                    "R-Resists" => $_[1]->{resist},
                };
            },
        ) if %$dispelOut;
        
        ##############
        # DISPELS IN #
        ##############

        $PAGE .= $pm->tableRows(
            title => "Auras Dispelled by Others",
            header => [ "Name", "R-Sources", "R-Casts", "R-Resists", ],
            data => $dispelIn,
            sort => sub ($$) { $_[1]->{count} <=> $_[0]->{count} },
            master => sub {
                return {
                    "Name" => $pm->spellLink( $_[0], "Dispels and Interrupts" ),
                    "R-Sources" => scalar keys %{$dispelIn->{$_[0]}},
                    "R-Casts" => $_[1]->{count},
                    "R-Resists" => $_[1]->{resist},
                };
            },
            slave => sub {
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "R-Casts" => $_[1]->{count},
                    "R-Resists" => $_[1]->{resist},
                };
            },
        ) if %$dispelIn;
        
        ##################
        # INTERRUPTS OUT #
        ##################

        $PAGE .= $pm->tableRows(
            title => "Casts Interrupted on Others",
            header => [ "Name", "R-Targets", "R-Casts", "", ],
            data => $interruptOut,
            sort => sub ($$) { $_[1]->{count} <=> $_[0]->{count} },
            master => sub {
                return {
                    "Name" => $pm->spellLink( $_[0], "Dispels and Interrupts" ),
                    "R-Targets" => scalar keys %{$interruptOut->{$_[0]}},
                    "R-Casts" => $_[1]->{count},
                };
            },
            slave => sub {
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "R-Casts" => $_[1]->{count},
                };
            },
        ) if %$interruptOut;
        
        #################
        # INTERRUPTS IN #
        #################

        $PAGE .= $pm->tableRows(
            title => "Casts Interrupted by Others",
            header => [ "Name", "R-Sources", "R-Casts", "", ],
            data => $interruptIn,
            sort => sub ($$) { $_[1]->{count} <=> $_[0]->{count} },
            master => sub {
                return {
                    "Name" => $pm->spellLink( $_[0], "Dispels and Interrupts" ),
                    "R-Sources" => scalar keys %{$interruptIn->{$_[0]}},
                    "R-Casts" => $_[1]->{count},
                };
            },
            slave => sub {
                return {
                    "Name" => $pm->actorLink( $_[0] ),
                    "R-Casts" => $_[1]->{count},
                };
            },
        ) if %$interruptIn;

        $PAGE .= $pm->tableEnd;
        $PAGE .= $pm->tabEnd;
    }
    
    ##########
    # DEATHS #
    ##########

    if( (!$do_group || !$self->{collapse} ) && $self->_keyExists( $self->{ext}{Death}{actors}, @PLAYER ) ) {
        my @header = (
            "Death Time",
            "R-Health",
            "Event",
        );

        # Loop through all deaths.
        my $n = 0;
        my %dnum;
        
        foreach my $player (@PLAYER) {
            foreach my $death (@{$self->{ext}{Death}{actors}{$player}}) {
                my $id = lc $death->{actor};
                $id = $self->{pm}->tameText($id);
                
                if( !$n++ ) {
                    $PAGE .= $pm->tabStart("Deaths");
                    $PAGE .= $pm->tableStart;
                    $PAGE .= $pm->tableHeader("Deaths", @header);
                }
                
                # Get the last line of the autopsy.
                my $lastline = $death->{autopsy}->[-1];
                my $text = $lastline ? $lastline->{event}->toString( 
                    sub { $self->{pm}->actorLink( $_[0], 1 ) }, 
                    sub { $self->{pm}->spellLink( $_[0] ) } 
                ) : "";

                my $t = $death->{t} - $raidStart;
                $PAGE .= $pm->tableRow(
                    header => \@header,
                    data => {
                        "Death Time" => $death->{t} && sprintf( "%02d:%02d.%03d", $t/60, $t%60, ($t-floor($t))*1000 ),
                        "R-Health" => $lastline->{hp} || "",
                        "Event" => $text,
                    },
                    type => "master",
                    url => sprintf( "death_%s_%d.json", $id, ++$dnum{ $death->{actor} } ),
                );

                # Print subsequent rows.
                foreach my $line (@{$death->{autopsy}}) {
                    $PAGE .= $pm->tableRow(
                        header => \@header,
                        data => {},
                        type => "slave",
                    );
                }
            }
        }

        if( $n ) {
            $PAGE .= $pm->tableEnd;
            $PAGE .= $pm->tabEnd;
        }
    }
    
    $PAGE .= $pm->jsTab( $is_raider && $eff_on_others && $eff_on_others > 2 * ($dmg_to_mobs||0) ? "Healing" : $tabs[0]) if @tabs;
    $PAGE .= $pm->tabBarEnd;
    
    ##########
    # FOOTER #
    ##########
    
    $PAGE .= $pm->pageFooter;
}

sub _decodespell {
    my $self = shift;
    my $encoded_spellid = shift;
    my $pm = shift;
    my $tab = shift;
    my @PLAYER = @_;
    
    my $spellactor;
    my $spellname;
    my $spellid;

    if( $encoded_spellid =~ /^([A-Za-z0-9]+): (.+)$/ ) {
        if( ! grep $_ eq $1, @PLAYER ) {
            $spellactor = $1;
            $spellname = sprintf( "%s: %s", $pm->actorLink( $1 ), $pm->spellLink( $2, $tab ) );
            $spellid = $2;
        } else {
            $spellactor = $1;
            $spellname = $pm->spellLink( $2, $tab );
            $spellid = $2;
        }
    } else {
        $spellactor = $PLAYER[0];
        $spellname = $pm->spellLink( $encoded_spellid, $tab );
        $spellid = $encoded_spellid;
    }
    
    return ($spellactor, $spellname, $spellid);
}

sub _abilityRows {
    my $self = shift;
    my $eOut = shift;
    
    # We want to make this a two-dimensional spell + target hash
    my %ret;
    
    while( my ($kactor, $vactor) = each(%$eOut) ) {
        while( my ($kspell, $vspell) = each(%$vactor) ) {
            # Encoded spell name.
            my $espell = "$kactor: $kspell";
            
            while( my ($ktarget, $vtarget) = each(%$vspell) ) {
                # Add a reference to this leaf.
                $ret{ $espell }{ $ktarget } = $vtarget;
            }
        }
    }
    
    return \%ret;
}

sub _targetRows {
    my $self = shift;
    my $eOut = shift;

    # We want to make this a two-dimensional target + spell hash
    my %ret;

    while( my ($kactor, $vactor) = each(%$eOut) ) {
        while( my ($kspell, $vspell) = each(%$vactor) ) {
            # Encoded spell name.
            my $espell = "$kactor: $kspell";
            
            while( my ($ktarget, $vtarget) = each(%$vspell) ) {
                # Add a reference to this leaf.
                $ret{ $ktarget }{ $espell } = $vtarget;
            }
        }
    }

    return \%ret;
}

sub _keyExists {
    my $self = shift;
    my $ext = shift;
    my @PLAYER = @_;
    
    foreach (@PLAYER) {
        if( exists $ext->{$_} ) {
            return 1;
        }
    }
    
    return 0;
}

1;
