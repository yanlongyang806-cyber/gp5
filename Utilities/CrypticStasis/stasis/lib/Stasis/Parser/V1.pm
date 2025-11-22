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

package Stasis::Parser::V1;

use strict;
use warnings;

use Stasis::Parser;
use Stasis::Event qw/%action_map/;
our @ISA = "Stasis::Parser";

# Returns compact hashes for v1 logs.
sub parse {
    my ($self, $line) = @_;
    
    # Pull the stamp out.
    my $t;
    ($t, $line) = $self->_pullStamp( $line );
    if( !$t ) {
        return {
            action => 0,
            actor => 0,
            actor_name => "",
            actor_relationship => 0,
            target => 0,
            target_name => "",
            target_relationship => 0,
        };
    }
    
    my %result;
    
    #############################
    # VERSION 1 LOGIC (PRE-2.4) #
    #############################
    
    if( $line =~ /^(.+) fades from (.+)\.$/ ) {
        # AURA FADE
        %result = $self->_legacyAction(
            "SPELL_AURA_REMOVED",
            undef,
            $2,
            {
                spellid => $1,
                spellname => $1,
                spellschool => undef,
                auratype => undef,
            }
        );
	} elsif( $line =~ /^(.+) (?:gain|gains) ([0-9]+) (Happiness|Rage|Mana|Energy|Focus) from (?:(you)r|(.+?)\s*'s) (.+)\.$/ ) {
	    # POWER GAIN WITH SOURCE
	    %result = $self->_legacyAction(
            "SPELL_ENERGIZE",
            $4 ? $4 : $5,
            $1,
            {
                spellid => $6,
                spellname => $6,
                spellschool => undef,
                amount => $2,
                powertype => lc $3,
            }
        );
    } elsif( $line =~ /^(.+) (?:gain|gains) ([0-9]+) (Happiness|Rage|Mana|Energy|Focus) from (.+)\.$/ ) {
	    # POWER GAIN WITHOUT SOURCE
	    %result = $self->_legacyAction(
            "SPELL_ENERGIZE",
            $1,
            $1,
            {
                spellid => $4,
                spellname => $4,
                spellschool => undef,
                amount => $2,
                powertype => lc $3,
            }
        );
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+?) drains ([0-9]+) Mana from ([^\.]+)\. .+ (?:gain|gains) [0-9]+ Mana\.$/ ) {
        # MANA LEECH
        %result = $self->_legacyAction(
            "SPELL_LEECH",
            $1 ? $1 : $2,
            $5,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                amount => $4,
                powertype => "mana",
                extraamount => 0,
            }
        );
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+?) drains ([0-9]+) Mana from ([^\.]+)\.$/ ) {
        # MANA DRAIN
        %result = $self->_legacyAction(
            "SPELL_DRAIN",
            $1 ? $1 : $2,
            $5,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                amount => $4,
                powertype => "mana",
                extraamount => 0,
            }
        );
    } elsif( $line =~ /^(.+) (?:gain|gains) ([0-9]+) health from (?:(you)r|(.+?)\s*'s) (.+)\.$/ ) {
        # HOT HEAL WITH SOURCE
        %result = $self->_legacyAction(
            "SPELL_PERIODIC_HEAL",
            $3 ? $3 : $4,
            $1,
            {
                spellid => $5,
                spellname => $5,
                spellschool => undef,
                amount => $2,
                critical => undef,
            }
        );
    } elsif( $line =~ /^(.+) (?:gain|gains) ([0-9]+) health from (.+)\.$/ ) {
        # HOT HEAL WITHOUT SOURCE
        %result = $self->_legacyAction(
            "SPELL_PERIODIC_HEAL",
            $1,
            $1,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                amount => $2,
                critical => undef,
            }
        );
    } elsif( $line =~ /^(.+) (?:gain|gains) ([0-9]+) extra (?:attack|attacks) through (.+)\.$/ ) {
        # EXTRA ATTACKS
        %result = $self->_legacyAction(
            "SPELL_EXTRA_ATTACKS",
            $1,
            $1,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                amount => $2,
            }
        );
    } elsif( $line =~ /^(.+) (?:gain|gains) (.+)\.$/ ) {
        # BUFF GAIN
        %result = $self->_legacyAction(
            "SPELL_AURA_APPLIED",
            undef,
            $1,
            {
                spellid => $2,
                spellname => $2,
                spellschool => undef,
                auratype => "BUFF",
            }
        );
        
        # Remove doses from the name
        $result{spellid} =~ s/ \([0-9]+\)$//;
        $result{spellname} = $result{spellid};
    } elsif( $line =~ /^(.+) (?:is|are) afflicted by (.+)\.$/ ) {
        # DEBUFF GAIN
        %result = $self->_legacyAction(
            "SPELL_AURA_APPLIED",
            undef,
            $1,
            {
                spellid => $2,
                spellname => $2,
                spellschool => undef,
                auratype => "DEBUFF",
            }
        );
        
        # Remove doses from the name
        $result{spellid} =~ s/ \([0-9]+\)$//;
        $result{spellname} = $result{spellid};
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+) causes (.+) ([0-9]+) damage\.\w*(.*?)$/ ) {
        # CAUSED DAMAGE (e.g. SOUL LINK)
        
        %result = $self->_legacyAction(
            "DAMAGE_SPLIT",
            $1 ? $1 : $2,
            $4,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                amount => $5,
                school => undef,
                resisted => undef,
                blocked => undef,
                absorbed => undef,
                critical => undef,
                glancing => undef,
                crushing => undef,
            }
        );
        
        # Assign mods
        my $mods = $self->_parseMods($6);
        $result{resisted} = $mods->{resistValue} if $mods->{resistValue};
        $result{absorbed} = $mods->{absorbValue} if $mods->{absorbValue};
        $result{blocked} = $mods->{blockValue} if $mods->{blockValue};
        $result{crushing} = $mods->{crush} if $mods->{crush};
        $result{glancing} = $mods->{glance} if $mods->{glance};
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+) (crits|crit|hit|hits) (.+) for ([0-9]+)( [a-zA-Z]+ damage|)\.\w*(.*?)$/ ) {
        # DIRECT YELLOW HIT (SPELL OR MELEE)
        %result = $self->_legacyAction(
            "SPELL_DAMAGE",
            $1 ? $1 : $2,
            $5,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                amount => $6,
                school => undef,
                resisted => undef,
                blocked => undef,
                absorbed => undef,
                critical => undef,
                glancing => undef,
                crushing => undef,
            }
        );
        
        # Check if it was a critical
        if( $4 eq "crits" || $4 eq "crit" ) {
            $result{critical} = 1;
        }
        
        # Assign mods
        my $mods = $self->_parseMods($8);
        $result{resisted} = $mods->{resistValue} if $mods->{resistValue};
        $result{absorbed} = $mods->{absorbValue} if $mods->{absorbValue};
        $result{blocked} = $mods->{blockValue} if $mods->{blockValue};
        $result{crushing} = $mods->{crush} if $mods->{crush};
        $result{glancing} = $mods->{glance} if $mods->{glance};
    } elsif( $line =~ /^(.+) (crits|crit|hit|hits) (.+) for ([0-9]+)( [a-zA-Z]+ damage|)\.\w*(.*?)$/ ) {
        # DIRECT WHITE HIT (MELEE)
        %result = $self->_legacyAction(
            "SWING_DAMAGE",
            $1,
            $3,
            {
                amount => $4,
                school => undef,
                resisted => undef,
                blocked => undef,
                absorbed => undef,
                critical => undef,
                glancing => undef,
                crushing => undef,
            }
        );
        
        # Check if it was a critical
        if( $2 eq "crits" || $2 eq "crit" ) {
            $result{critical} = 1;
        }
        
        # Assign mods
        my $mods = $self->_parseMods($6);
        $result{resisted} = $mods->{resistValue} if $mods->{resistValue};
        $result{absorbed} = $mods->{absorbValue} if $mods->{absorbValue};
        $result{blocked} = $mods->{blockValue} if $mods->{blockValue};
        $result{crushing} = $mods->{crush} if $mods->{crush};
        $result{glancing} = $mods->{glance} if $mods->{glance};
    } elsif( $line =~ /^(.+) (?:attack|attacks)\. (.+) (?:block|blocks)\.$/ ) {
        # WHITE FULL BLOCK
        %result = $self->_legacyAction(
            "SWING_MISSED",
            $1,
            $2,
            {
                misstype => "BLOCK",
            }
        );
    } elsif( $line =~ /^(.+) (?:attack|attacks)\. (.+) (?:parry|parries)\.$/ ) {
        # WHITE PARRY
        %result = $self->_legacyAction(
            "SWING_MISSED",
            $1,
            $2,
            {
                misstype => "PARRY",
            }
        );
    } elsif( $line =~ /^(.+) (?:attack|attacks)\. (.+) (?:dodge|dodges)\.$/ ) {
        # WHITE DODGE
        %result = $self->_legacyAction(
            "SWING_MISSED",
            $1,
            $2,
            {
                misstype => "DODGE",
            }
        );
    } elsif( $line =~ /^(.+) (?:attack|attacks)\. (.+) (?:absorb|absorbs) all the damage\.$/ ) {
        # WHITE FULL ABSORB
        %result = $self->_legacyAction(
            "SWING_MISSED",
            $1,
            $2,
            {
                misstype => "ABSORB",
            }
        );
    } elsif( $line =~ /^(.+) (?:miss|misses) (.+)\.$/ ) {
        # WHITE MISS
        %result = $self->_legacyAction(
            "SWING_MISSED",
            $1,
            $2,
            {
                misstype => "MISS",
            }
        );
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+) (?:is parried|was parried)( by .+|)\.$/ ) {
        # YELLOW PARRY
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $1 ? $1 : $2,
            undef,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                misstype => "PARRY",
            }
        );
        
        # Figure out target.
        my $target = $4;
        if( $target && $target =~ /^ by (.+)$/ ) {
            $target = $1;
        } else {
            $target = "you";
        }
        
        $result{target} = $result{target_name} = $target;
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+) was dodged( by .+|)\.$/ ) {
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $1 ? $1 : $2,
            undef,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                misstype => "DODGE",
            }
        );
        
        # Figure out target.
        my $target = $4;
        if( $target && $target =~ /^ by (.+)$/ ) {
            $target = $1;
        } else {
            $target = "you";
        }
        
        $result{target} = $result{target_name} = $target;
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+) was resisted( by .+|)\.$/ ) {
        # YELLOW RESIST
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $1 ? $1 : $2,
            undef,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                misstype => "RESIST",
            }
        );
        
        # Figure out target.
        my $target = $4;
        if( $target && $target =~ /^ by (.+)$/ ) {
            $target = $1;
        } else {
            $target = "you";
        }
        
        $result{target} = $result{target_name} = $target;
    } elsif( $line =~ /^(.+) resists (?:(You)r|(.+?)\s*'s) (.+)\.$/ ) {
        # YELLOW RESIST, ALTERNATE FORMAT
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $2 ? $2 : $3,
            $1,
            {
                spellid => $4,
                spellname => $4,
                spellschool => undef,
                misstype => "RESIST",
            }
        );
    } elsif( $line =~ /^(.+) was resisted by (.+)\.$/ ) {
        # WHITE RESIST
        %result = $self->_legacyAction(
            "SWING_MISSED",
            $1,
            $2,
            {
                misstype => "RESIST",
            }
        );

        # Figure out target.
        my $target = $4;
        if( $target && $target =~ /^ by (.+)$/ ) {
            $target = $1;
        } else {
            $target = "you";
        }

        $result{target} = $result{target_name} = $target;
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+) (?:missed|misses) (.+)\.$/ ) {
        # YELLOW MISS
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $1 ? $1 : $2,
            $4,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                misstype => "MISS",
            }
        );
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+) was blocked( by .+|)\.$/ ) {
        # YELLOW FULL BLOCK
        # (Is this what a self block looks like?)
        
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $1 ? $1 : $2,
            undef,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                misstype => "BLOCK",
            }
        );
        
        # Figure out target.
        my $target = $4;
        if( $target && $target =~ /^ by (.+)$/ ) {
            $target = $1;
        } else {
            $target = "you";
        }
        
        $result{target} = $result{target_name} = $target;
    } elsif( $line =~ /^You absorb (?:(you)r|(.+?)\s*'s) (.+)\.$/ ) {
        # YELLOW FULL ABSORB, SELF
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $1 ? $1 : $2,
            "you",
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                misstype => "ABSORB",
            }
        );
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+) is absorbed by (.+)\.$/ ) {
        # YELLOW FULL ABSORB, OTHER
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $1 ? $1 : $2,
            $4,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                misstype => "ABSORB",
            }
        );
    } elsif( $line =~ /^(.+) (?:suffer|suffers) ([0-9]+) (\w+) damage from (?:(you)r|(.+?)\s*'s) (.+)\.\w*(.*?)$/ ) {
        # YELLOW DOT WITH SOURCE
        %result = $self->_legacyAction(
            "SPELL_PERIODIC_DAMAGE",
            $4 ? $4 : $5,
            $1,
            {
                spellid => $6,
                spellname => $6,
                spellschool => undef,
                amount => $2,
                school => undef,
                resisted => undef,
                blocked => undef,
                absorbed => undef,
                critical => undef,
                glancing => undef,
                crushing => undef,
            }
        );
        
        # Assign mods
        my $mods = $self->_parseMods($7);
        $result{resisted} = $mods->{resistValue} if $mods->{resistValue};
        $result{absorbed} = $mods->{absorbValue} if $mods->{absorbValue};
        $result{blocked} = $mods->{blockValue} if $mods->{blockValue};
        $result{crushing} = $mods->{crush} if $mods->{crush};
        $result{glancing} = $mods->{glance} if $mods->{glance};
    } elsif( $line =~ /^(.+) (?:suffer|suffers) ([0-9]+) (\w+) damage from (.+)\.\w*(.*?)$/ ) {
        # YELLOW DOT WITHOUT SOURCE
        %result = $self->_legacyAction(
            "SPELL_PERIODIC_DAMAGE",
            $1,
            $1,
            {
                spellid => $4,
                spellname => $4,
                spellschool => undef,
                amount => $2,
                school => undef,
                resisted => undef,
                blocked => undef,
                absorbed => undef,
                critical => undef,
                glancing => undef,
                crushing => undef,
            }
        );
        
        # Assign mods
        my $mods = $self->_parseMods($5);
        $result{resisted} = $mods->{resistValue} if $mods->{resistValue};
        $result{absorbed} = $mods->{absorbValue} if $mods->{absorbValue};
        $result{blocked} = $mods->{blockValue} if $mods->{blockValue};
        $result{crushing} = $mods->{crush} if $mods->{crush};
        $result{glancing} = $mods->{glance} if $mods->{glance};
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+?) (critically heals|heals) (.+) for ([0-9]+)\.$/ ) {
        # HEAL
        %result = $self->_legacyAction(
            "SPELL_HEAL",
            $1 ? $1 : $2,
            $5,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                amount => $6,
                critical => $4 eq "critically heals" ? 1 : undef,
            }
        );
    } elsif( $line =~ /^(.+) (?:begins|begin) to (?:cast|perform) (.+)\.$/ ) {
        # CAST START
        %result = $self->_legacyAction(
            "SPELL_CAST_START",
            $1,
            undef,
            {
                spellid => $2,
                spellname => $2,
                spellschool => undef,
            }
        );
    } elsif( $line =~ /^(.+) (?:fail|fails) to (?:cast|perform) (.+): (.+)\.$/ ) {
        # CAST FAILURE
        %result = $self->_legacyAction(
            "SPELL_CAST_FAILED",
            $1,
            undef,
            {
                spellid => $2,
                spellname => $2,
                spellschool => undef,
                misstype => $3,
            }
        );
    } elsif( $line =~ /^(.+) (?:cast|casts|perform|performs) (.+)\.$/ ) {
        # CAST SUCCESS
        my $actor = $1;
        my $target;
        my $spell;
        
        # Split the performance into target and spell, maybe.
        my $performance = $2;
        if( $performance =~ /^(.+) on (.+)$/ ) {
            $target = $2;
            $spell = $1;
        } else {
            $spell = $performance;
        }
        
        # Create the action.
        %result = $self->_legacyAction(
            "SPELL_CAST_SUCCESS",
            $actor,
            $target,
            {
                spellid => $spell,
                spellname => $spell,
                spellschool => undef,
            }
        );
    } elsif( $line =~ /^(.+) (?:dies|die|is destroyed)\.$/ ) {
        # DEATH
        %result = $self->_legacyAction(
            "UNIT_DIED",
            undef,
            $1,
            {
                
            }
        );
    } elsif( $line =~ /^(.+) (?:is|are) killed by (.+)\.$/ ) {
        # KILL (e.g. DEMONIC SACRIFICE)
        %result = $self->_legacyAction(
            "SPELL_INSTAKILL",
            undef,
            $1,
            {
                spellid => $2,
                spellname => $2,
                spellschool => undef,
            }
        );
    } elsif( $line =~ /^(.+) (?:fall|falls) and (?:lose|loses) ([0-9]+) health\.$/ ) {
        # FALL DAMAGE
        %result = $self->_legacyAction(
            "ENVIRONMENTAL_DAMAGE",
            undef,
            $1,
            {
                environmentaltype => "FALLING",
                amount => $2,
                school => undef,
                resisted => undef,
                blocked => undef,
                absorbed => undef,
                critical => undef,
                glancing => undef,
                crushing => undef,
            }
        );
    } elsif( $line =~ /^(.+) (?:interrupt|interrupts) (.+?)\s*'s (.+)\.$/ ) {
        # INTERRUPT
        %result = $self->_legacyAction(
            "SPELL_INTERRUPT",
            $1,
            $2,
            {
                spellid => undef,
                spellname => undef,
                spellschool => undef,
                extraspellid => $3,
                extraspellname => $3,
                extraspellschool => undef,
            }
        );
    } elsif( $line =~ /^(.+) (?:reflect|reflects) ([0-9]+) (\w+) damage to (.+)\.\w*(.*?)$/ ) {
        # MELEE REFLECT (e.g. THORNS)
        %result = $self->_legacyAction(
            "DAMAGE_SHIELD",
            $1,
            $4,
            {
                spellid => "Reflect",
                spellname => "Reflect",
                spellschool => undef,
                amount => $2,
                school => undef,
                resisted => undef,
                blocked => undef,
                absorbed => undef,
                critical => undef,
                glancing => undef,
                crushing => undef,
            }
        );
        
        my $mods = $self->_parseMods($5);
        $result{resisted} = $mods->{resistValue} if $mods->{resistValue};
        $result{absorbed} = $mods->{absorbValue} if $mods->{absorbValue};
        $result{blocked} = $mods->{blockValue} if $mods->{blockValue};
        $result{crushing} = $mods->{crush} if $mods->{crush};
        $result{glancing} = $mods->{glance} if $mods->{glance};
    } elsif( $line =~ /^(?:(You)r|(.+?)\s*'s) (.+?) (?:fails|failed)\.\s+(.+) (?:are|is) immune\.$/ ) {
        # YELLOW IMMUNITY
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $1 ? $1 : $2,
            $4,
            {
                spellid => $3,
                spellname => $3,
                spellschool => undef,
                misstype => "IMMUNE",
            }
        );
    } elsif( $line =~ /^(.+) (?:is|are) immune to (?:(you)r|(.+?)\s*'s) (.+)\.$/ ) {
        # YELLOW IMMUNITY, ALTERNATE FORMAT
        %result = $self->_legacyAction(
            "SPELL_MISSED",
            $2 ? $2 : $3,
            $1,
            {
                spellid => $4,
                spellname => $4,
                spellschool => undef,
                misstype => "IMMUNE",
            }
        );
    } elsif( $line =~ /^(.+) (?:attacks|attack) but (.+) (?:are|is) immune\.$/ ) {
        # WHITE IMMUNITY
        %result = $self->_legacyAction(
            "SWING_MISSED",
            $1,
            $2,
            {
                misstype => "IMMUNE",
            }
        );
    } elsif( $line =~ /^(.+) (?:fails|failed)\. (.+) (?:are|is) immune\.$/ ) {
        # SINGLE-WORD IMMUNITY (e.g. DOOMFIRE)
        %result = $self->_legacyAction(
            "SWING_MISSED",
            $1,
            $2,
            {
                misstype => "IMMUNE",
            }
        );
    } else {
        # Unrecognized action
        %result = $self->_legacyAction(
            "",
            "",
            "",
            {}
        );
    }
    
    # Replace action with a number
    $result{action} = $action_map{$result{action}} || 0;
    
    # Replace "You" with name of the logger
    $result{actor} = $self->{logger} if $result{actor} && lc $result{actor} eq "you";
    $result{target} = $self->{logger} if $result{target} && lc $result{target} eq "you";
    
    $result{actor_name} = $self->{logger} if $result{actor_name} && lc $result{actor_name} eq "you";
    $result{target_name} = $self->{logger} if $result{target_name} && lc $result{target_name} eq "you";
    
    # Write in the time
    $result{t} = $t;
    
    # Replace undefined actor or target with blank
    if( !$result{actor_name} ) {
        $result{actor} = 0;
        $result{actor_relationship} = 0;
        $result{actor_name} = "";
    }
    
    if( !$result{target_name} ) {
        $result{target} = 0;
        $result{target_relationship} = 0;
        $result{target_name} = "";
    }
    
    # Replace other undefs with zeros
    foreach my $rkey ( keys %result ) {
        if( !defined($result{$rkey}) ) {
            $result{$rkey} = 0;
        }
    }
    
    my $ret = \%result;
    bless $ret, "Stasis::Event";
}

sub _parseMods {
    my ($self, $mods) = @_;
    
    my %result = ();
    
    # figure out mods
    if( $mods ) {
        while( $mods =~ /\(([^\)]+)\)/g ) {
            my $mod = $1;
            if( $mod =~ /^([0-9]+) (.+)$/ ) {
                # numeric mod
                if( $2 eq "blocked" ) {
                    $result{blockValue} = $1;
                } elsif( $2 eq "absorbed" ) {
                    $result{absorbValue} = $1;
                } elsif( $2 eq "resisted" ) {
                    $result{resistValue} = $1;
                }
            } else {
                # text mod
                if( $mod eq "crushing" ) {
                    $result{crush} = 1;
                } elsif( $mod eq "glancing" ) {
                    $result{glance} = 1;
                }
            }
        }
    }
    
    return \%result;
}

sub _legacyAction {
    my ($self, $action, $actor, $target, $extra) = @_;
    
    return (
        action => $action,
        actor => $actor,
        actor_name => $actor,
        actor_relationship => 0,
        target => $target,
        target_name => $target,
        target_relationship => 0,
        %$extra
    );
}

my $stamp_regex = qr/^(\d+)\/(\d+) (\d+):(\d+):(\d+)\.(\d+)  (.*?)[\r\n]*$/s;
sub _pullStamp {
    my ($self, $line) = @_;
    
    if( $line =~ $stamp_regex ) {
        return 
            POSIX::mktime( 
                $5, # sec
                $4, # min
                $3, # hour
                $2, # mday
                $1 - 1, # mon
                $self->{year} - 1900, # year
                0, # wday
                0, # yday
                -1 # is_dst
            ) + $6/1000,
            $7;
    } else {
        # Couldn't recognize time
        return (0, $line);
    }
}

1;
