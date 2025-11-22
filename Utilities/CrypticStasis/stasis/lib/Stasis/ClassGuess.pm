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

package Stasis::ClassGuess;

use strict;
use warnings;
use POSIX;
use Carp;

use Stasis::EventDispatcher;
use Stasis::MobUtil;
use Stasis::SpellUtil;

# various giveaway pet spells
my %pet_fingerprints = (
    SPELL_PERIODIC_ENERGIZE => {
        1539 => "target",     # Feed Pet Effect
    },

    SPELL_ENERGIZE => {
        34952 => "target",    # Go for the Throat (r1)
        34953 => "target",    # Go for the Throat (r2)
        32553 => "target",    # Life Tap
        54607 => "target",    # Soul Leech Mana
    },

    SPELL_LEECH => {
        27265 => "target",    # Dark Pact (r4)
        59092 => "target",    # Dark Pact (r5)
    },

    SPELL_INSTAKILL => {
        18788 => "target",    # Demonic Sacrifice
    },

    DAMAGE_SPLIT => {
        25228 => "target",    # Soul Link
    },

    SPELL_AURA_APPLIED => {
        47283 => "actor",     # Empowered Imp (all ranks)
        57484 => "actor",     # Kindred Spirits (?)
        23826 => "actor",     # Master Demonologist (imp, 2pt)
        23827 => "actor",     # Master Demonologist (imp, 3pt)
        23828 => "actor",     # Master Demonologist (imp, 4pt)
        23829 => "actor",     # Master Demonologist (imp, 5pt)
        35702 => "actor",     # Master Demonologist (felguard, 1pt)
        35703 => "actor",     # Master Demonologist (felguard, 2pt)
        35704 => "actor",     # Master Demonologist (felguard, 3pt)
        35705 => "actor",     # Master Demonologist (felguard, 4pt)
        35706 => "actor",     # Master Demonologist (felguard, 5pt)
        19574 => "target",    # Bestial Wrath (all ranks)
        27259 => "target",    # Health Funnel (r8)
        47856 => "target",    # Health Funnel (r9)
        43771 => "target",    # Well Fed (Kibler's Bits and Spiced Mammoth Treats)
        25228 => "actor",     # Soul Link
    },

    SPELL_PERIODIC_HEAL => {
        19579 => "actor",     # Spirit Bond (r1)
        24529 => "actor",     # Spirit Bond (r2)
        27046 => "target",    # Mend Pet (r8)
        48989 => "target",    # Mend Pet (r9)
        48990 => "target",    # Mend Pet (r10)
    },

    SPELL_HEAL => {
        54181 => "target",    # Fel Synergy (all ranks)
    }
);

sub new {
    my ($class, %params) = @_;
    bless { debug => $params{debug} }, $class;
}

sub assign_pet {
    my ($self, $reason, $owner, $pet) = @_;
    
    if( !$self->{class}{$pet} ) {
        $self->{class}{$pet} = "Pet";
        $self->{pets}{$owner}{$pet}++;

        printf STDERR "ClassGuess: %s: %s => %s\n",
          $reason,
          ( $self->{index}{$pet}   ? $self->{index}{$pet} . " ($pet)"     : $pet ),
          ( $self->{index}{$owner} ? $self->{index}{$owner} . " ($owner)" : $owner ),
          if $self->{debug};
    }
}

sub register {
    my ( $self, $ed ) = @_;
    
    # process_misc identifies players and some pets
    $ed->add( qw/SPELL_DAMAGE SPELL_PERIODIC_DAMAGE SPELL_HEAL SPELL_PERIODIC_HEAL SPELL_CAST_SUCCESS/,
        sub { $self->process_misc( @_ ) } );
    
    # process_summon captures SUMMON events and turns them into pets
    $ed->add( qw/SPELL_SUMMON/, sub { $self->process_summon( @_ ) } );
    
    # look for giveaway spells
    while( my ( $action, $spells ) = each( %pet_fingerprints ) ) {
        my @actions = ref $action eq 'ARRAY' ? @$action : ($action);
        
        foreach my $a (@actions) {
            $ed->add(
                $a,
                sub {
                    my ( $event ) = @_;
                    return unless $event->{actor} && $event->{target} && $event->{actor} ne $event->{target};

                    while( my ( $spell, $k ) = each( %$spells ) ) {
                        if( $event->{spellid} == $spell && !$self->{class}{ $event->{$k} } ) {
                            $self->assign_pet( "PETFP", $event->{ $k eq 'actor' ? 'target' : 'actor' }, $event->{$k} );
                        }
                    }
                }
            );
        }
    }
    
    $ed->add(
        "SPELL_AURA_REMOVED",
        sub {
            my ( $event ) = @_;
            return unless $event->{actor} && $event->{target};
            
            $self->{index_name}{ $event->{target_name} }{ $event->{target} } ||= 1;
            $self->{index}{ $event->{target} } ||= $event->{target_name};
        }
    );
    
    # Gigantic hacks
    $self->register_doomguards( $ed );
    $self->register_armyofthedead( $ed );
}

sub process_misc {
    my ( $self, $event ) = @_;
    
    # Skip if actor and target are not set.
    return unless $event->{actor} && $event->{target};
    
    # Think about classifying the actor.
    if( !$self->{class}{ $event->{actor} } ) {
        # Get the type.
        my ($atype, $anpc, $aspawn ) = Stasis::MobUtil::splitguid( $event->{actor} );

        if($event->{cryptic})
        {
            if( ($event->{actor_relationship} & 0x500) == 0x500 ) {
                $self->{class}{ $event->{actor} } = 'NNOPlayer';
            }
        }
        else
        {
        # See if this actor is a player.
        if( ($event->{actor_relationship} & 0x500) == 0x500 ) {
            my $spell = Stasis::SpellUtil->spell( $event->{spellid} );
            if( $spell && $spell->{class} ) {
                $self->{class}{ $event->{actor} } = $spell->{class};
                
                printf STDERR "ClassGuess: CLASS: %s: %s (%s %s)\n",
                  $spell->{class},
                  $event->actorf( "%s (%g)" ),
                  $event->actionName,
                  $event->{spellname},
                  if $self->{debug};
            }
        }
        }
        
        # Index this actor by name, just in case we want to do name-based assignment later.
        $self->{index_name}{ $event->{actor_name} }{ $event->{actor} } ||= 1;
        $self->{index}{ $event->{actor} } ||= $event->{actor_name};
        
        # Greater Fire and Earth elementals (pre-2.4.3 code)
        if( !$self->{class}{ $event->{actor} } && $event->{target} ne $event->{actor} && ( $anpc == 15438 || $anpc == 15352 ) ) {
            while( my ($totemid, $shamanid) = each(%{$self->{totems}}) ) {
                # Associate totem with this elemental by consecutive spawncount.
                my @totem = Stasis::MobUtil::splitguid( $totemid );
                my @elemental = Stasis::MobUtil::splitguid( $event->{actor} );
                if( $totem[2] + 1 == $elemental[2] ) {
                    $self->assign_pet( "GELEM", $shamanid, $event->{actor} );
                }
            }
        }
    }
}

sub process_summon {
    my ($self, $event) = @_;
    
    if( !$self->{class}{ $event->{target} } ) {
        # Follow the pet chain.
        my $owner = $event->{actor};
        while( $self->{class}{ $owner } && $self->{class}{ $owner } eq "Pet" ) {
            # Find the pet's owner.
            foreach my $kpet (keys %{$self->{pets}}) {
                if( $self->{pets}{ $kpet }{ $owner } ) {
                    $owner = $kpet;
                    last;
                }
            }
        }

        $self->assign_pet( "SUMMN", $owner, $event->{target} );

        # Shaman elemental totems (Fire and Earth respectively)
        if( $event->{spellid} == 2894 || $event->{spellid} == 2062 ) {
            # Associate totem with shaman by SPELL_SUMMON event.
            $self->{totems}{ $event->{target} } = $event->{actor};
        }
    }
}

sub register_doomguards {
    my ( $self, $ed ) = @_;
    
    # As of patch 3.0.8 (and also some patches before that) doomguard summons look like this:
    
    # 1/22 00:01:37.741  SPELL_DAMAGE,0x00000000010C5AC6,"Adoucir",0x514,0x00000000010C5AC6,"Adoucir",0x514,20625,"Ritual of Doom Sacrifice",0x1,14253,0,1,0,0,0,nil,nil,nil
    # 1/22 00:01:38.428  SPELL_AURA_APPLIED,0xF130002E53009F84,"Doomguard",0x1114,0xF130002E53009F84,"Doomguard",0x1114,22987,"Ritual Enslavement",0x1,BUFF
    # 1/22 00:01:38.428  SPELL_CAST_SUCCESS,0xF130002E53009F84,"Doomguard",0x1114,0x0000000000000000,nil,0x80000000,22987,"Ritual Enslavement",0x1
    # 1/22 00:01:38.444  SPELL_CAST_SUCCESS,0xF130002E53009F84,"Doomguard",0x1114,0x0000000000000000,nil,0x80000000,42010,"Doomguard Spawn (DND)",0x1

    # Look for "Ritual of Doom Sacrifice" (spellid 20625), which goes from a warlock onto a player
    $ed->add(
        "SPELL_DAMAGE",
        sub {
            my ( $event ) = @_;
            $self->{_doomlock}{ $event->{actor} } = $event->{t} if( $event->{spellid} == 20625 );
        }
    );

    # Look for "Doomguard Spawn (DND)" (spellid 42010), which doomguards cast on the environment when they spawn
    $ed->add(
        "SPELL_CAST_SUCCESS",
        sub {
            my ( $event ) = @_;
            if( $event->{spellid} == 42010 ) {
                # look for warlocks that summoned a doomguard within the last two seconds
                my @doomlocks =
                  grep { $self->{_doomlock}{$_} <= $event->{t} && $self->{_doomlock}{$_} > ( $event->{t} - 2 ) }
                  keys %{ $self->{_doomlock} };

                if( @doomlocks == 1 ) {
                    # exactly one match! nice.
                    $self->assign_pet( "DOOMG", $doomlocks[0], $event->{actor} );
                }
            }
        }
    );
}

sub register_armyofthedead {
    my ( $self, $ed ) = @_;

    # As of patch 3.0.8 (and also some patches before that) AotD summons look like this:

    # 1/21 22:12:04.018  SPELL_AURA_APPLIED,0x0000000002F84DBC,"Wastedknight",0x10518,0x0000000002F84DBC,"Wastedknight",0x10518,42650,"Army of the Dead",0x20,BUFF
    # 1/21 22:12:04.020  SPELL_CAST_SUCCESS,0x0000000002F84DBC,"Wastedknight",0x10518,0x0000000000000000,nil,0x80000000,42650,"Army of the Dead",0x20
    # 1/21 22:12:10.005  SPELL_AURA_REMOVED,0x0000000002F84DBC,"Wastedknight",0x10518,0x0000000002F84DBC,"Wastedknight",0x10518,42650,"Army of the Dead",0x20,BUFF
    # 1/21 22:12:39.246  SPELL_CAST_SUCCESS,0x0000000002BD7395,"Gianluca",0x511,0xF130005E8F119844,"Army of the Dead Ghoul",0x12118,48441,"Rejuvenation",0x8
    # 1/21 22:12:39.249  SPELL_AURA_APPLIED,0x0000000002BD7395,"Gianluca",0x511,0xF130005E8F119844,"Army of the Dead Ghoul",0x12118,48441,"Rejuvenation",0x8,BUFF

    # Look for "Army of the Dead" (spellid 42650), which is cast by a death knight:
    $ed->add(
        "SPELL_CAST_SUCCESS",
        sub {
            my ( $event ) = @_;
            return unless $event->{actor};

            if( $event->{spellid} == 42650 ) {
                $self->{_aotdk}{ $event->{actor} } = $event->{t};
            }
        }
    );

  # Look for melee hits by "Army of the Dead Ghoul" (npc 24207), and assign it to a DK if there's only one that matches.
    $ed->add(
        "SWING_DAMAGE",
        sub {
            my ( $event ) = @_;
            return unless $event->{actor};

            if( !$self->{class}{ $event->{actor} } ) {
                my ( undef, $anpc, undef ) = Stasis::MobUtil::splitguid( $event->{actor} );

                if( $anpc == 24207 && !$self->{_aotdbl}{ $event->{actor} } ) {
                    my @aotdks =
                      grep { $self->{_aotdk}{$_} <= $event->{t} && $self->{_aotdk}{$_} > ( $event->{t} - 20 ) }
                      keys %{ $self->{_aotdk} };

                    if( @aotdks == 1 ) {
                        # exactly one DK cast AotD in the past 20 seconds, assign to that one
                        $self->assign_pet( "AROTD", $aotdks[0], $event->{actor} );
                    } elsif( @aotdks > 1 ) {
                        # blacklist this ghoul, otherwise it will get stolen when the window expires
                        $self->{_aotdbl}{ $event->{actor} } = 1;
                    }
                }
            }
        }
    );
}

sub finish {
    my ( $self ) = @_;
    
    # We will eventually return this list of raid members.
    # Keys will be raid member IDs and values will be two element hashes
    # Each hash will have at least two keys: "class" (a string) and "pets" (an array of pet IDs)
    my %raid;
    
    # This temporarily stores owners.
    my %powner;
    
    # Add non-pets
    while( my ($actorid, $actorclass) = each (%{$self->{class}})) {
        next if $actorclass eq "Pet";
        
        $raid{$actorid} = {
            class => $actorclass,
            pets => [],
        };
    }
    
    # Add positively identified pets
    while( my ($actorid, $pethash) = each (%{$self->{pets}})) {
        if( exists $raid{$actorid} ) {
            push @{$raid{$actorid}{pets}}, keys %$pethash;
            
            foreach my $petid (keys %$pethash) {
                $powner{$petid} = $actorid;
                $raid{$petid}{class} = "Pet";
            }
        }
    }
    
    # It's probably okay to assign pets based on mob name alone if:
    # a) Everything with that name is either unidentified, or the pet of the same player
    # b) All actors with that name have the same NPC type and ID.
    # c) Not a generic pet type like doomguards or army of the dead or something.
    
    foreach my $kowner (keys %raid) {
        my $vowner = $raid{$kowner};
        next unless $vowner->{pets} && @{$vowner->{pets}};
        
        # See if we should give this guy extra pets.
        my %pet_names_seen;
        my @pet_names = grep { defined $_ && ! $pet_names_seen{$_}++ } map { $self->{index}{$_} } @{$vowner->{pets}};
        
        PETNAME: foreach my $pet_name (@pet_names) {
            next unless $self->{index_name}{$pet_name};

            my $npcid;
            foreach my $candidate_guid (keys %{ $self->{index_name}{$pet_name} }) {
                # Condition A
                next PETNAME if $powner{$candidate_guid} && $powner{$candidate_guid} ne $kowner;
                next PETNAME if exists $raid{$candidate_guid} && $raid{$candidate_guid}{class} ne "Pet";

                # Condition B
                my ( undef, $_npc, undef ) = Stasis::MobUtil::splitguid $candidate_guid;
                if( ! defined $npcid ) {
                    $npcid = $_npc;
                } else {
                    next PETNAME if $npcid != $_npc;
                }
                
                # Condition C
                next PETNAME if $_npc == 11859; # doomguard
                next PETNAME if $_npc == 24207; # army of the dead
            }
            
            # Conditions passed, assign these actors as pets.
            foreach my $candidate_guid (keys %{ $self->{index_name}{$pet_name} }) {
                if( ! exists $raid{$candidate_guid} ) {
                    # since we already created %raid, we have to muck it up here
                    # we're not going to save the information for posterity, which is why we don't call assign_pet
                    
                    # the reason is that sometimes finish() will be called again later after more processing, and
                    # if we screw up here we don't want to record that forever.
                    
                    $raid{$candidate_guid}{class} = "Pet";
                    $powner{$candidate_guid} = $kowner;
                    push @{$vowner->{pets}}, $candidate_guid;
                    
                    printf STDERR "ClassGuess: PNAME: %s => %s\n",
                      (
                          $self->{index}{$candidate_guid}
                        ? $self->{index}{$candidate_guid} . " ($candidate_guid)"
                        : $candidate_guid
                      ),
                      ( $self->{index}{$kowner} ? $self->{index}{$kowner} . " ($kowner)" : $kowner ),
                      if $self->{debug};
                }
            }
        }
    }
    
    return %raid;
}

1;
