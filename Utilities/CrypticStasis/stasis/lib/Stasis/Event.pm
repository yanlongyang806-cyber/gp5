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

package Stasis::Event;

use strict;
use warnings;

use Stasis::MobUtil qw/splitguid/;

# Constants from the 2.4 combat log
our %action_map;

BEGIN {
    %action_map = (
        SWING_DAMAGE => 1,
        SWING_MISSED => 2,
        RANGE_DAMAGE => 3,
        RANGE_MISSED => 4,
        SPELL_DAMAGE => 5,
        SPELL_MISSED => 6,
        SPELL_HEAL => 7,
        SPELL_ENERGIZE => 8,
        SPELL_PERIODIC_MISSED => 9,
        SPELL_PERIODIC_DAMAGE => 10,
        SPELL_PERIODIC_HEAL => 11,
        SPELL_PERIODIC_DRAIN => 12,
        SPELL_PERIODIC_LEECH => 13,
        SPELL_PERIODIC_ENERGIZE => 14,
        SPELL_DRAIN => 15,
        SPELL_LEECH => 16,
        SPELL_INTERRUPT => 17,
        SPELL_EXTRA_ATTACKS => 18,
        SPELL_INSTAKILL => 19,
        SPELL_DURABILITY_DAMAGE => 20,
        SPELL_DURABILITY_DAMAGE_ALL => 21,
        SPELL_DISPEL_FAILED => 22,
        SPELL_AURA_DISPELLED => 23,
        SPELL_AURA_STOLEN => 24,
        SPELL_AURA_APPLIED => 25,
        SPELL_AURA_REMOVED => 26,
        SPELL_AURA_APPLIED_DOSE => 27,
        SPELL_AURA_REMOVED_DOSE => 28,
        SPELL_CAST_START => 29,
        SPELL_CAST_SUCCESS => 30,
        SPELL_CAST_FAILED => 31,
        DAMAGE_SHIELD => 32,
        DAMAGE_SHIELD_MISSED => 33,
        ENCHANT_APPLIED => 34,
        ENCHANT_REMOVED => 35,
        ENVIRONMENTAL_DAMAGE => 36,
        DAMAGE_SPLIT => 37,
        UNIT_DIED => 38,
        SPELL_SUMMON => 39,
        SPELL_CREATE => 40,
        PARTY_KILL => 41,
        UNIT_DESTROYED => 42,
        SPELL_AURA_REFRESH => 43,
        SPELL_AURA_BROKEN_SPELL => 44,
        SPELL_DISPEL => 45,
        SPELL_STOLEN => 46,
        SPELL_AURA_BROKEN => 47,
        SPELL_RESURRECT => 48,
        SPELL_BUILDING_DAMAGE => 49,
    );
}    

use Exporter qw/import/;
our @EXPORT_OK = ( '%action_map', keys %action_map );
our %EXPORT_TAGS = ( "constants" => [keys %action_map] );

use constant \%action_map;

sub new {
    my ( $class, $ref ) = @_;
    bless $ref, $class;
}

sub timeString {
    my ( $self ) = @_;
    
    $self->{t} =~ /^(\d+)(\.(\d+)|)$/;
    my @t = localtime $1;
    sprintf "%d/%d %02d:%02d:%02d\.%03d", $t[4]+1, $t[3], $t[2], $t[1], $t[0], $2?$2*1000:0;
}

sub oldFormat {
    my ($self) = @_;
    
    my $h = {};
    
    foreach my $k (keys %$self) {
        if( grep { $k eq $_ } qw/t actor actor_name actor_relationship target target_name target_relationship/ ) {
            $h->{$k} = $self->{$k};
        } elsif( $k eq "action" ) {
            $h->{$k} = $self->actionName;
        } else {
            $h->{extra}{$k} = $self->{$k};
        }
    }
    
    return $h;
}

# TODO -- this method blows, lots of repeated code and it's not complete
sub toString {
    my ($self, $actor_callback, $spell_callback) = @_;
    my $event = $self;
    
    my $actor =
      ( $event->{actor} && $actor_callback )
      ? $actor_callback->( $event->{actor} )
      : ( $event->{actor_name} || "Environment" );
    
    my $target =
      ( $event->{target} && $actor_callback )
      ? $actor_callback->( $event->{target} )
      : ( $event->{target_name} || "Environment" );
    
    my $spell =
      ( $event->{spellid} && $spell_callback )
      ? $spell_callback->( $event->{spellid}, $event->{spellname} )
      : ( $event->{spellname} );
      
    my $extraspell =
      ( $event->{extraspellid} && $spell_callback )
      ? $spell_callback->( $event->{extraspellid}, $event->{spellname} )
      : ( $event->{extraspellname} );
    
    my $text = "";
    
    if( $event->{action} == SWING_DAMAGE ) {
        $text = sprintf "[%s] %s [%s] %d",
            $actor,
            $event->{critical} ? "crit" : "hit",
            $target,
            $event->{amount};
        
        $text .= sprintf " (%d resisted)", $event->{resisted} if $event->{resisted};
        $text .= sprintf " (%d blocked)", $event->{blocked} if $event->{blocked};
        $text .= sprintf " (%d absorbed)", $event->{absorbed} if $event->{absorbed};
        $text .= " (crushing)" if $event->{crushing};
        $text .= " (glancing)" if $event->{glancing};
        
        # WLK log overdamage
        if( $event->{extraamount} ) {
            $text .= sprintf " {%s}", $event->{extraamount};
        }
    } elsif( $event->{action} == SWING_MISSED ) {
        $text = sprintf "[%s] melee [%s] %s",
            $actor,
            $target,
            lc( $event->{misstype} );
    } elsif( $event->{action} == RANGE_DAMAGE ) {
        $text = sprintf "[%s] %s %s [%s] %d",
            $actor,
            $spell,
            $event->{critical} ? "crit" : "hit",
            $target,
            $event->{amount};
        
        $text .= sprintf " (%d resisted)", $event->{resisted} if $event->{resisted};
        $text .= sprintf " (%d blocked)", $event->{blocked} if $event->{blocked};
        $text .= sprintf " (%d absorbed)", $event->{absorbed} if $event->{absorbed};
        $text .= " (crushing)" if $event->{crushing};
        $text .= " (glancing)" if $event->{glancing};
        
        # WLK log overdamage
        if( $event->{extraamount} ) {
            $text .= sprintf " {%s}", $event->{extraamount};
        }
    } elsif( $event->{action} == RANGE_MISSED ) {
        $text = sprintf "[%s] %s [%s] %s",
            $actor,
            $spell,
            $target,
            lc( $event->{misstype} );
    } elsif( $event->{action} == SPELL_DAMAGE ) {
        $text = sprintf "[%s] %s %s [%s] %d",
            $actor,
            $spell,
            $event->{critical} ? "crit" : "hit",
            $target,
            $event->{amount};
        
        $text .= sprintf " (%d resisted)", $event->{resisted} if $event->{resisted};
        $text .= sprintf " (%d blocked)", $event->{blocked} if $event->{blocked};
        $text .= sprintf " (%d absorbed)", $event->{absorbed} if $event->{absorbed};
        $text .= " (crushing)" if $event->{crushing};
        $text .= " (glancing)" if $event->{glancing};
        
        # WLK log overdamage
        if( $event->{extraamount} ) {
            $text .= sprintf " {%s}", $event->{extraamount};
        }
    } elsif( $event->{action} == SPELL_MISSED ) {
        $text = sprintf "[%s] %s [%s] %s",
            $actor,
            $spell,
            $target,
            lc( $event->{misstype} );
    } elsif( $event->{action} == SPELL_HEAL ) {
        $text = sprintf "[%s] %s %s [%s] %d",
            $actor,
            $spell,
            $event->{critical} ? "crit heal" : "heal",
            $target,
            $event->{amount};
        
        # WLK log overhealing
        if( $event->{extraamount} ) {
            $text .= sprintf " {%s}", $event->{extraamount};
        }
    } elsif( $event->{action} == SPELL_ENERGIZE ) {
        $text = sprintf "[%s] %s energize [%s] %d %s",
            $actor,
            $spell,
            $target,
            $event->{amount},
            $self->powerName();
    } elsif( $event->{action} == SPELL_PERIODIC_MISSED ) {
        $text = sprintf "[%s] %s [%s] %s",
            $actor,
            $spell,
            $target,
            lc( $event->{misstype} );
    } elsif( $event->{action} == SPELL_PERIODIC_DAMAGE ) {
        $text = sprintf "[%s] %s %s [%s] %d",
            $actor,
            $spell,
            $event->{critical} ? "crit dot" : "dot",
            $target,
            lc( $event->{amount} );
        
        $text .= sprintf " (%d resisted)", $event->{resisted} if $event->{resisted};
        $text .= sprintf " (%d blocked)", $event->{blocked} if $event->{blocked};
        $text .= sprintf " (%d absorbed)", $event->{absorbed} if $event->{absorbed};
        $text .= " (crushing)" if $event->{crushing};
        $text .= " (glancing)" if $event->{glancing};
        
        # WLK log overdamage
        if( $event->{extraamount} ) {
            $text .= sprintf " {%s}", $event->{extraamount};
        }
    } elsif( $event->{action} == SPELL_PERIODIC_HEAL ) {
        $text = sprintf "[%s] %s %s [%s] %d",
            $actor,
            $spell,
            $event->{critical} ? "crit hot" : "hot",
            $target,
            $event->{amount};
        
        # WLK log overhealing
        if( $event->{extraamount} ) {
            $text .= sprintf " {%s}", $event->{extraamount};
        }
    } elsif( $event->{action} == SPELL_PERIODIC_DRAIN ) {
        $text = sprintf "[%s] %s drain [%s] %d %s",
            $actor,
            $spell,
            $target,
            $event->{amount},
            $self->powerName();
    } elsif( $event->{action} == SPELL_PERIODIC_LEECH ) {
        $text = sprintf "[%s] %s leech [%s] %d %s",
            $actor,
            $spell,
            $target,
            $event->{amount},
            $self->powerName();
    } elsif( $event->{action} == SPELL_PERIODIC_ENERGIZE ) {
        $text = sprintf "[%s] %s energize [%s] %d %s",
            $actor,
            $spell,
            $target,
            $event->{amount},
            $self->powerName();
    } elsif( $event->{action} == SPELL_DRAIN ) {
        $text = sprintf "[%s] %s drain [%s] %d %s",
            $actor,
            $spell,
            $target,
            $event->{amount},
            $self->powerName();
    } elsif( $event->{action} == SPELL_LEECH ) {
        $text = sprintf "[%s] %s leech [%s] %d %s",
            $actor,
            $spell,
            $target,
            $event->{amount},
            $self->powerName();
    } elsif( $event->{action} == SPELL_INTERRUPT ) {
        $text = sprintf "[%s] %sinterrupt [%s] %s",
            $actor,
            $spell ? $spell . " " : "",
            $target,
            $extraspell,
    } elsif( $event->{action} == SPELL_EXTRA_ATTACKS ) {
        $text = sprintf "[%s] %s +%d attack%s",
            $actor,
            $spell,
            $event->{amount},
            $event->{amount} > 1 ? "s" : "",
    } elsif( $event->{action} == SPELL_INSTAKILL ) {
        $text = sprintf "[%s] instakill [%s]",
            $actor,
            $target,
    } elsif( $event->{action} == SPELL_DURABILITY_DAMAGE ) {

    } elsif( $event->{action} == SPELL_DURABILITY_DAMAGE_ALL ) {

    } elsif( $event->{action} == SPELL_DISPEL_FAILED ) {
        $text = sprintf "[%s] %sfail to dispel [%s] %s",
            $actor,
            $spell ? $spell . " " : "",
            $target,
            $extraspell,
    } elsif( $event->{action} == SPELL_AURA_DISPELLED ) {
        $text = sprintf "[%s] %sdispel [%s] %s",
            $actor,
            $spell ? $spell . " " : "",
            $target,
            $extraspell,
    } elsif( $event->{action} == SPELL_AURA_STOLEN ) {
        $text = sprintf "[%s] steal [%s] %s",
            $actor,
            $target,
            $spell;
    } elsif( $event->{action} == SPELL_AURA_APPLIED ) {
        $text = sprintf "[%s] %s [%s] %s",
            $target,
            $event->{auratype} eq "DEBUFF" ? "afflicted by" : "gained",
            $actor,
            $spell;
    } elsif( $event->{action} == SPELL_AURA_REMOVED ) {
        $text = sprintf "[%s] fade [%s] %s",
            $target,
            $actor,
            $spell;
    } elsif( $event->{action} == SPELL_AURA_APPLIED_DOSE ) {
        $text = sprintf "[%s] %s [%s] %s (%d)",
            $target,
            $event->{auratype} eq "DEBUFF" ? "afflicted by" : "gained",
            $actor,
            $spell,
            $event->{amount};
    } elsif( $event->{action} == SPELL_AURA_REMOVED_DOSE ) {
        $text = sprintf "[%s] decrease dose [%s] %s (%d)",
            $target,
            $actor,
            $spell,
            $event->{amount};
    } elsif( $event->{action} == SPELL_CAST_START ) {
        $text = sprintf "[%s] start casting %s",
            $actor,
            $spell;
    } elsif( $event->{action} == SPELL_CAST_SUCCESS ) {
        $text = sprintf "[%s] cast %s [%s]",
            $actor,
            $spell,
            $target;
    } elsif( $event->{action} == SPELL_CAST_FAILED ) {
        $text = sprintf "[%s] fail to cast %s (%s)",
            $actor,
            $spell,
            $event->{misstype};
    } elsif( $event->{action} == DAMAGE_SHIELD ) {
        $text = sprintf "[%s] %s reflect %s[%s] %d",
            $actor,
            $spell,
            $event->{critical} ? "crit " : "",
            $target,
            $event->{amount};
        
        $text .= sprintf " (%d resisted)", $event->{resisted} if $event->{resisted};
        $text .= sprintf " (%d blocked)", $event->{blocked} if $event->{blocked};
        $text .= sprintf " (%d absorbed)", $event->{absorbed} if $event->{absorbed};
        $text .= " (crushing)" if $event->{crushing};
        $text .= " (glancing)" if $event->{glancing};
    } elsif( $event->{action} == DAMAGE_SHIELD_MISSED ) {
        $text = sprintf "[%s] %s [%s] %s",
            $actor,
            $spell,
            $target,
            lc( $event->{misstype} );
    } elsif( $event->{action} == ENCHANT_APPLIED ) {

    } elsif( $event->{action} == ENCHANT_REMOVED ) {

    } elsif( $event->{action} == ENVIRONMENTAL_DAMAGE ) {

    } elsif( $event->{action} == DAMAGE_SPLIT ) {
        $text = sprintf "[%s] %s %s [%s] %d (split)",
            $actor,
            $spell,
            $event->{critical} ? "crit" : "hit",
            $target,
            $event->{amount};
        
        $text .= sprintf " (%d resisted)", $event->{resisted} if $event->{resisted};
        $text .= sprintf " (%d blocked)", $event->{blocked} if $event->{blocked};
        $text .= sprintf " (%d absorbed)", $event->{absorbed} if $event->{absorbed};
        $text .= " (crushing)" if $event->{crushing};
        $text .= " (glancing)" if $event->{glancing};
    } elsif( $event->{action} == UNIT_DIED ) {
        $text = sprintf "[%s] dies",
            $target;
    } elsif( $event->{action} == SPELL_RESURRECT ) {
        $text = sprintf "[%s] %s resurrect [%s]",
            $actor,
            $spell,
            $target;
    } elsif( $event->{action} == SPELL_AURA_REFRESH ) {
        $text = sprintf "[%s] refresh [%s] %s",
            $actor,
            $target,
            $spell;
    }
    
    return $text;
}

sub actorf {
    my ($self, $format) = @_;
    $self->xf( $format, "actor" );
}

sub targetf {
    my ($self, $format) = @_;
    $self->xf( $format, "target" );
}

sub xf {
    my ($self, $format, $key) = @_;
    
    $format =~ s/%g/ $self->{$key} /eg;
    $format =~ s/%s/ $self->{"${key}_name"} /eg;
    $format =~ s/%i/ ( splitguid $self->{$key} )[1] /eg;
    return $format;
}

sub powerName {
    my ($self, $use_this) = @_;
    my $code = @_ > 1 ? $use_this : $self->{powertype};
    
    if( !defined $code ) {
        return "power";
    } elsif( $code == 0 ) {
        return "mana";
    } elsif( $code == 1 ) {
        return "rage";
    } elsif( $code == 2 ) {
        return "focus";
    } elsif( $code == 3 ) {
        return "energy";
    } elsif( $code == 4 ) {
        return "happiness";
    } elsif( $code == 5 ) {
        return "runes";
    } elsif( $code == 6 ) {
        return "runic power";
    } elsif( $code == -2 ) {
        return "health";
    } else {
        return "$code (?)";
    }
}

{
    my %reverse_action_map;
    @reverse_action_map{ values %action_map } = keys %action_map;

    sub actionName {
        my ( $self, $use_this ) = @_;
        return $reverse_action_map{ defined $use_this ? $use_this : $self->{action} };
    }
}

1;
