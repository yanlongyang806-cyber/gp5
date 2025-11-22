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

package Stasis::Page;

use strict;
use warnings;
use POSIX;
use HTML::Entities;
use Stasis::PageMaker;
use Stasis::ActorGroup;

sub new {
    my $class = shift;
    my %params = @_;
    
    my $self = {
        raid => $params{raid} || {},
        ext => $params{ext} || {},
        index => $params{index} || {},
        grouper => $params{grouper} || Stasis::ActorGroup->new->run( $params{raid}, $params{ext} ),
        collapse => $params{collapse},
        name => $params{name} || "Untitled",
        server => $params{server} || "",
        dirname => $params{dirname} || "",
        short => $params{short} || "",
    };
    
    $self->{pm} = $params{pm} || Stasis::PageMaker->new;
    $self->{pm}{$_} = $self->{$_} foreach qw/index raid ext grouper collapse/;
    
    bless $self, $class;
}

sub page {
    my $self = shift;
    return $self->{pm}->pageHeader() . $self->{pm}->pageFooter();
}

# Escape for double quotes
sub _dqesc {
    my $str = pop;
    
    $str = "$str";
    $str =~ s/([\t\r\n\/\\\"])/\\$1/g;
    return $str;
}

sub _tidypct {
    my $n = pop;
    
    if( $n ) {
        if( floor($n) == $n ) {
            return sprintf "%d", $n;
        } else {
            return sprintf "%0.1f", $n;
        }
    } else {
        return 0;
    }
}

sub _tidymag {
    my $n = pop;
    
    if( !$n ) {
        return 0;
    } elsif( $n < 1_000 ) {
        return sprintf "%d", $n;
    } elsif( $n < 100_000 ) {
        return sprintf "%0.1fK", $n/1_000;
    } else {
        return sprintf "%0.1fM", $n/1_000_000;
    }
}

sub _cricruglaText {
    my $sdata = pop;
    
    my $swings = ($sdata->{count}||0) - ($sdata->{tickCount}||0);
    my $ticks = $sdata->{tickCount}||0;
    my $tickCrits = $sdata->{tickCritCount}||0;
    return unless $swings || $tickCrits;
    
    my $pct;
    my @text;
    
    # if there were ticking swings, record the percentage as the main one
    # record crushes and glances as extra lines
    if( $swings ) {
        $pct = _tidypct(($sdata->{critCount}||0) / $swings * 100 );

        my @atype = qw/crushing glancing/;
        foreach my $type (@atype) {
            push @text, _tidypct( $sdata->{$type} / $swings * 100 ) . "% $type" if $sdata->{$type};
        }
    }
    
    # if there were ticking crits, record the percentage (either as the main one or as an extra line)
    if( $tickCrits ) {
        my $tickpct = _tidypct( $tickCrits / $ticks * 100 );
        if( ! $pct ) {
            $pct = $tickpct;
        } else {
            push @text, "${pct}% of direct damage";
            push @text, "${tickpct}% of dots";
        }
    }
    
    # make things look pretty
    $pct &&= "$pct%";
    $pct ||= "0%" if @text;
    
    return ($pct, @text ? join( ";", @text ) : undef );
}

sub _avoidanceText {
    my $sdata = pop;
    
    my $swings = ($sdata->{count}||0) - ($sdata->{tickCount}||0);
    return unless $swings;
    
    my $pct = _tidypct( 100 - ( ($sdata->{hitCount}||0) + ($sdata->{critCount}||0) - ($sdata->{glancing}||0) ) / $swings * 100 );
    $pct &&= "$pct%";
    
    my @text;
    my @atype = qw/miss dodge parry block absorb resist immune/;
    
    foreach my $type (@atype) {
        push @text, _tidypct( $sdata->{$type . "Count"} / $swings * 100 ) . "% total $type" if $sdata->{$type . "Count"};
    }
    
    my $f = 1;
    my @ptype = qw/block resist absorb/;
    foreach (@ptype) {
        my $type = $_;
        $type =~ s/^(\w)/"partial" . uc $1/e;
        push @text, "" if $sdata->{$type . "Count"} && @text && $f++ == 1;
        push @text, _tidypct( $sdata->{$type . "Count"} / $sdata->{count} * 100 ) . "% partial ${_} (avg " . int($sdata->{$type . "Total"}/$sdata->{$type . "Count"}) . ")" if $sdata->{count} && $sdata->{$type . "Count"};
    }
    
    if( @text ) {
        $pct ||= "0%";
    }
    
    return ($pct, @text ? join( ";", @text ) : undef );
}

sub _addHCT {
    my ($self, $sdata, $suffix) = @_;
    return ($sdata->{"hit$suffix"}||0) + ($sdata->{"crit$suffix"}||0) + ($sdata->{"tick$suffix"}||0);
}

sub _rowDamage {
    my ($self, $sdata, $mnum, $header, $title, $time) = @_;
    
    # We're printing a row based on $sdata.
    my $swings = ($sdata->{count}||0) - ($sdata->{tickCount}||0);
    
    return {
        ($header || "Ability") => $title,
        "R-Total" => $sdata->{total},
        "R-%" => $sdata->{total} && $mnum && _tidypct( $sdata->{total} / $mnum * 100 ),
        "R-DPS" => $sdata->{total} && $time && sprintf( "%d", $sdata->{total}/$time ),
        "R-Time" => $time && sprintf( "%02d:%02d", $time/60, $time%60 ),
        "R-Direct" => (($sdata->{hitCount}||0) + ($sdata->{critCount}||0)) && $self->{pm}->tip( sprintf( "%d", ($sdata->{hitCount}||0) + ($sdata->{critCount}||0) ), join( "<br />", map { $sdata->{$_ . "Count"} ? $sdata->{$_ . "Count"} . " ${_}s" : () } qw/hit crit/ ) ),
        "R-Hits" => $sdata->{hitCount} && sprintf( "%d", $sdata->{hitCount} ),
        "R-AvHit" => $sdata->{hitCount} && $sdata->{hitTotal} && $self->{pm}->tip( int($sdata->{hitTotal} / $sdata->{hitCount}), sprintf( "Range: %d&ndash;%d", $sdata->{hitMin}, $sdata->{hitMax} ) ),
        "R-Ticks" => $sdata->{tickCount} && sprintf( "%d", $sdata->{tickCount} ),
        "R-AvTick" => $sdata->{tickCount} && $sdata->{tickTotal} && $self->{pm}->tip( int($sdata->{tickTotal} / $sdata->{tickCount}), sprintf( "Range: %d&ndash;%d", $sdata->{tickMin}, $sdata->{tickMax} ) ),
        "R-Crits" => $sdata->{critCount} && sprintf( "%d", $sdata->{critCount} ),
        "R-AvCrit" => $sdata->{critCount} && $sdata->{critTotal} && $self->{pm}->tip( int($sdata->{critTotal} / $sdata->{critCount}), sprintf( "Range: %d&ndash;%d", $sdata->{critMin}, $sdata->{critMax} ) ),
        "R-% Crit" => $self->{pm}->tip( _cricruglaText($sdata) ),
        "R-Avoid" => $self->{pm}->tip( _avoidanceText($sdata) ),
    };
}

sub _rowHealing {
    my ($self, $sdata, $mnum, $header, $title) = @_;
    
    # We're printing a row based on $sdata.
    return {
        ($header || "Ability") => $title,
        "R-Eff. Heal" => $sdata->{effective}||0,
        "R-%" => $sdata->{effective} && $mnum && _tidypct( $sdata->{effective} / $mnum * 100 ),
        "R-Overheal" => $sdata->{total} && sprintf( "%0.1f%%", ($sdata->{total} - ($sdata->{effective}||0) ) / $sdata->{total} * 100 ),
        "R-Count" => $sdata->{count}||0,
        "R-Direct" => (($sdata->{hitCount}||0) + ($sdata->{critCount}||0)) && $self->{pm}->tip( sprintf( "%d", ($sdata->{hitCount}||0) + ($sdata->{critCount}||0) ), join( "<br />", map { $sdata->{$_ . "Count"} ? $sdata->{$_ . "Count"} . " ${_}s" : () } qw/hit crit/ ) ),
        "R-Hits" => $sdata->{hitCount} && sprintf( "%d", $sdata->{hitCount} ),
        "R-AvHit" => $sdata->{hitCount} && $sdata->{hitTotal} && $self->{pm}->tip( int($sdata->{hitTotal} / $sdata->{hitCount}), sprintf( "Range: %d&ndash;%d", $sdata->{hitMin}, $sdata->{hitMax} ) ),
        "R-Ticks" => $sdata->{tickCount} && sprintf( "%d", $sdata->{tickCount} ),
        "R-AvTick" => $sdata->{tickCount} && $sdata->{tickTotal} && $self->{pm}->tip( int($sdata->{tickTotal} / $sdata->{tickCount}), sprintf( "Range: %d&ndash;%d", $sdata->{tickMin}, $sdata->{tickMax} ) ),
        "R-Crits" => $sdata->{critCount} && sprintf( "%d", $sdata->{critCount} ),
        "R-AvCrit" => $sdata->{critCount} && $sdata->{critTotal} && $self->{pm}->tip( int($sdata->{critTotal} / $sdata->{critCount}), sprintf( "Range: %d&ndash;%d", $sdata->{critMin}, $sdata->{critMax} ) ),
        "R-% Crit" => $sdata->{count} && $sdata->{critCount} && ($sdata->{count} - ($sdata->{tickCount}||0)) && sprintf( "%0.1f%%", ($sdata->{critCount}||0) / ($sdata->{count} - ($sdata->{tickCount}||0)) * 100 ),
    };
}

sub _json {
    my ($self, $ds) = @_;
    
    if( ref $ds eq 'HASH' ) {
        '{' . join( ',', map { "\"$_\":" . $self->_json( $ds->{$_} ) } keys %$ds ) . '}'
    } elsif( ref $ds eq 'ARRAY' ) {
        '[' . join( ',', map { $self->_json( $_ ) } @$ds ) . ']'
    } elsif( $ds ) {
        '"' . $self->_dqesc( $ds ) . '"'
    } else {
        '""'
    }
}

1;
