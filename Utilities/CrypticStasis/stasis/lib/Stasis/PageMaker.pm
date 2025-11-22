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

package Stasis::PageMaker;

use strict;
use warnings;
use POSIX;
use HTML::Entities qw();
use Stasis::Extension qw/ext_sum/;
use Stasis::Page;
use Carp;

sub new {
    my $class = shift;
    my %params = @_;
    
    # Section ID
    $params{id} = 0;
    
    # Tooltip ID
    $params{tid} = 0;
    
    # Header ID
    $params{headid} = 0;
    
    # Current Tab ID
    $params{tabid} = 0;
    
    # Row ID (for odd/even coloring)
    $params{rowid} = 0;
    
    bless \%params, $class;
}

sub tabBar {
    my $self = shift;
    
    my $BAR;
    $BAR .= "<div class=\"tabContainer\">";
    $BAR .= "<div class=\"tabBar\">";
    
    foreach my $tab (@_) {
        my $tametab = $self->tameText($tab);

        $BAR .= $self->alink( "#$tametab", $tab, { id => "tablink_$tametab", onclick => "toggleTab('$tametab')" } );
    }
    
    $BAR .= "</div>";
}

sub tabBarEnd {
    return "</div>";
}

sub tabStart {
    my $self = shift;
    my $name = shift;
    
    my $id = $self->tameText($name);
    
    $self->{headid} = 0;
    $self->{tabid} = $id;
    
    return "<div class=\"tab\" id=\"tab_$id\">";
}

sub tabEnd {
    my $self = shift;
    
    undef $self->{tabid};
    return "</div>";
}

sub tableStart {
    my $self = shift;
    my $class = shift;
    
    $class ||= "stat";
    
    return "<table cellspacing=\"0\" class=\"$class\">";
}

sub tableEnd {
    return "</table><br />";
}

sub tableTitle {
    my $self = shift;
    my $title = shift;
    
    my $style_text = "";
    if( $self->{headid}++ == 0 ) {
        $style_text .= " titlenoclear";
    }
    
    my $aname = $self->{tabid} ? $self->{tabid} . "0t" . $self->{headid} : "t" . $self->{headid};
    
    return sprintf "<tr><th class=\"title%s\" colspan=\"%d\">%s</th></tr>", $style_text, scalar @_, $self->alink( "#$aname", $title, { name => $aname } );
}

# tableHeader( @header_rows )
sub tableHeader {
    my ( $self, $title, @header ) = @_;

    my $result = "";
    $result = $self->tableTitle( $title, @header ) if $title;    
    $result .= "<tr>";
    
    # Reset row number (odd/even coloring)
    $self->{rowid} = 0;
    
    foreach my $col (@header) {
        my $style_text = "";
        if( $col =~ /^R-/ ) {
            $style_text .= "text-align: right;";
        }
        
        if( $col =~ /-W$/ ) {
            $style_text .= "white-space: normal; width: 300px;";
        }
        
        if( $style_text ) {
            $style_text = " style=\"${style_text}\"";
        }
        
        my $ncol = $col;
        $ncol =~ s/^R-//;
        $ncol =~ s/-W$//;
        $result .= sprintf "<th${style_text}>%s</th>", $ncol;
    }
    
    $result .= "</tr>";
}

# tableRow( %args )
sub tableRow {
    my $self = shift;
    my %params = @_;
    
    my $result;
    
    $params{header} ||= [];
    $params{data} ||= {};
    $params{type} ||= "";
    
    # Override 'name'
    $params{name} = $params{type} eq "master" ? ++$self->{id} : $self->{id};
    
    # Track rows within a table
    $self->{rowid}++ if $params{type} ne "slave";
    
    if( $params{type} eq "slave" ) {
        $result .= "<tr class=\"s\" name=\"s" . $params{name} . "\">";
    } elsif( $params{type} eq "master" ) {
        $result .= "<tr class=\"sectionMaster" . ($self->{rowid}%2==0 ? " odd" : "") . "\">";
    } else {
        $result .= "<tr class=\"section" . ($self->{rowid}%2==0 ? " odd" : "") . "\">";
    }
    
    my $firstflag;
    foreach my $col (@{$params{header}}) {
        my @class;
        my $align = "";
        
        if( !$firstflag ) {
            push @class, "f";
        }
        
        push @class, "r" if "R-" eq substr $col, 0, 2;
        push @class, "w" if "-W" eq substr $col, -2;
        
        if( @class ) {
            $align = " class=\"" . join( " ", @class ) . "\"";
        }
        
        if( $col eq " " && $params{data}{$col} ) {
            $params{data}{$col} = sprintf "<div class=\"chartbar\" style=\"width:%dpx\">&nbsp;</div>", $params{data}{$col};
        }
        
        if( !$firstflag && $params{type} eq "master" ) {
            # This is the first one (flag hasn't been set yet)
            $result .= sprintf "<td%s>(<a class=\"toggle\" id=\"as%s\" href=\"javascript:toggleTableSection(%s%s);\">+</a>) %s</td>", $align, $params{name}, $params{name}, $params{url} ? ",'" . $params{url} . "'" : "", $self->_commify($params{data}{$col});
        } else {
            if( $params{data}{$col} ) {
                $result .= "<td${align}>" . $self->_commify($params{data}{$col}) . "</td>";
            } else {
                $result .= "<td${align}></td>";
            }
        }
        
        $firstflag = 1;
    }
    
    $result .= "</tr>";
}

sub tableRows {
    my ($self, %params) = @_;
    
    # We'll return this.
    my $result;
    
    $params{title} ||= "";
    $params{slave} ||= $params{master};
    $params{master} ||= $params{slave};
    
    # Abort if we have no headers or data.
    return unless $params{master} && $params{data};
    
    # First make master rows. We have to do this first so they can be sorted.
    my %master;
    while( my ($kmaster, $vmaster) = each(%{$params{data}}) ) {
        if( scalar values %$vmaster > 1 ) {
            $master{$kmaster} = ext_sum( {}, values %$vmaster );
        } else {
            $master{$kmaster} = (values %$vmaster)[0];
        }
        
        if( $params{preprocess} ) {
            $params{preprocess}->( $kmaster, $master{$kmaster} );
            $params{preprocess}->( $_, $vmaster->{$_}, $kmaster, $master{$kmaster} ) foreach (keys %$vmaster);
        }
    }
    
    if( %master ) {
        # Print table header.
        $result .= $self->tableHeader( $params{title}, @{$params{header}} ) if $params{title};
        
        # Print rows.
        foreach my $kmaster ( $params{sort} ? sort { $params{sort}->( $master{$a}, $master{$b} ) } keys %master : keys %master ) {
            # Print master row.
            $result .= $self->tableRow( 
                header => $params{header},
                data => $params{master} ? $params{master}->($kmaster, $master{$kmaster}) : {
                    $params{header}->[0] => $kmaster,
                },
                type => "master",
            );

            # Print slave rows.
            my $vmaster = $params{data}{$kmaster};
            foreach my $kslave ( $params{sort} ? sort { $params{sort}->( $vmaster->{$a}, $vmaster->{$b} ) } keys %$vmaster : keys %$vmaster ) {
                $result .= $self->tableRow( 
                    header => $params{header},
                    data => $params{slave} ? $params{slave}->($kslave, $vmaster->{$kslave}, $kmaster, $master{$kmaster}) : {
                        $params{header}->[0] => $kslave,
                    },
                    type => "slave",
                );
            }
        }
    }
    
    return $result;
}

sub pageHeader {
    my $self = shift;
    my $title = shift || "Page";
    my $subtitle = shift || "";

    # Reset table row ID
    $self->{id} = 0;

    # Reset tip ID
    $self->{tid} = 0;
    
    # Add subtitle if it's nonempty
    $title .= " : $subtitle" if $subtitle;    
    return <<END;
<html>
<head>
<title>$title</title>
<!-- YUI: http://developer.yahoo.com/yui/articles/hosting/?connection&container&event&json&menu&MIN -->
<link rel="stylesheet" type="text/css" href="http://yui.yahooapis.com/combo?2.7.0/build/container/assets/skins/sam/container.css&2.7.0/build/menu/assets/skins/sam/menu.css"> 
<script type="text/javascript" src="http://yui.yahooapis.com/combo?2.7.0/build/yahoo-dom-event/yahoo-dom-event.js&2.7.0/build/connection/connection-min.js&2.7.0/build/container/container-min.js&2.7.0/build/json/json-min.js&2.7.0/build/menu/menu-min.js"></script>

<!-- SWS -->
<link rel="stylesheet" type="text/css" href="../extras/sws2.css" />
<script type="text/javascript" src="../extras/sws.js"></script>
</head>
<body>
<div class="yui-skin-sam">
<div class="swsmaster">
END
}

sub statHeader {
    my ($self, $title, undef, $start) = @_;
    
    # Start off with a header
    my $PAGE = "<div class=\"top\">";
    
    if( $start && $start =~ /^\d+(?:\.\d+|)$/ ) {
        # Header with time
        my $starttxt = $start ? ": " . HTML::Entities::encode_entities( strftime( "%a %B %d, %Y %H:%M:%S", localtime($start) ) ) : "";
        $PAGE .= "<h2>${title}${starttxt}</h2>" if $title;
        
        # Raid & Mobs menu
        my @raiders = map {
            my $link = $self->actorLink( $_ );
            $link =~ s/class="/class="yuimenuitemlabel /;
            sprintf '<li class="yuimenuitem">%s</li>', $link;
        } sort {
            ( $self->{raid}{$a}{class} cmp $self->{raid}{$b}{class} ) || ( $self->{index}->actorname($a) cmp $self->{index}->actorname($b) )
        } grep {
            exists $self->{raid}{$_} && $self->{raid}{$_}{class} && $self->{raid}{$_}{class} ne "Pet"
        } keys %{$self->{ext}{Presence}{actors}};
        
        my %group_seen;
        my @mobs = map {
            my $link = $self->actorLink( $_ );
            $link =~ s/class="/class="yuimenuitemlabel /;
            sprintf '<li class="yuimenuitem">%s</li>', $link;
        } sort {
            $self->{index}->actorname($a) cmp $self->{index}->actorname($b)
        } grep {
            my $group = $self->{grouper}->group($_);
            ! ( $group && $group_seen{$group} ++ );
        } grep {
            ! exists $self->{raid}{$_} || ! $self->{raid}{$_}{class}
        } keys %{$self->{ext}{Presence}{actors}};
        
        my $raid_text = join "", @raiders;
        my $mob_text = join "", @mobs;
        my $menu_init = Stasis::Page->_json( { bossnav => "raid.json" } );
        
        $PAGE .= <<MENU;
        <script type="text/javascript">initMenu('swsmenu', $menu_init);</script>
        <div id="swsmenu" class="yuimenubar yuimenubarnav">
            <div class="bd">
                <ul class="first-of-type">
                    <li id="bossnav" class="yuimenubaritem first-of-type"><a class="yuimenubaritemlabel first-of-type" href="index.html"><b>$title</b></a></li>

                    <li class="yuimenubaritem"><a class="yuimenubaritemlabel" href="index.html">Encounter</a>
                        <div id="charts" class="yuimenu">
                            <div class="bd">
                                <ul>
                                    <li class="yuimenuitem"><a class="yuimenuitemlabel" href="index.html#damage_out" onClick="toggleTab('damage_out',1)">Damage Out</a></li>
                                    <li class="yuimenuitem"><a class="yuimenuitemlabel" href="index.html#damage_in" onClick="toggleTab('damage_in',1)">Damage In</a></li>
                                    <li class="yuimenuitem"><a class="yuimenuitemlabel" href="index.html#healing" onClick="toggleTab('healing',1)">Healing</a></li>
                                    <li class="yuimenuitem"><a class="yuimenuitemlabel" href="index.html#deaths" onClick="toggleTab('deaths',1)">Deaths</a></li>
                                </ul>
                            </div>
                        </div>
                    </li>

                    <li id="raidandmobs" class="yuimenubaritem"><a class="yuimenubaritemlabel" href="index.html#raid__amp__mobs" onClick="toggleTab('raid__amp__mobs',1)">Raid &amp; Mobs</a>
                        <div id="raid" class="yuimenu">
                            <div class="bd">
                                <ul>
                                    <li class="yuimenuitem">
                                        <a class="yuimenuitemlabel" href="index.html#raid__amp__mobs" onClick="toggleTab('raid__amp__mobs',1)">Mobs</a>
                                        <div id="mobs" class="yuimenu">
                                            <div class="bd">
                                                <ul class="first-of-type">
                                                    $mob_text
                                                </ul>            
                                            </div>
                                        </div>
                                    </li>
                                </ul>
                                <ul>
                                    $raid_text
                                </ul>
                            </div>
                        </div>
                    </li>
                </ul>
            </div>
        </div>
MENU
    } else {
        $PAGE .= "<h2>${title}</h2>";
        $PAGE .= "<h4>" . ( $start || "" ) . "</h4>";
    }
    
    $PAGE .= "</div>";
    $PAGE =~ s/\s\s\s*//g;
    
    return $PAGE;
}

# pageFooter()
sub pageFooter {
    my $self = shift;
    my $timestr = HTML::Entities::encode_entities( strftime( "%a %B %d, %Y %H:%M:%S", localtime ) );
    
    return <<END;
<p class="footer">Generated on $timestr</p>
<p class="footer">stasiscl available at <a href="http://code.google.com/p/stasiscl/">http://code.google.com/p/stasiscl/</a></p>
</div>
</div>
<script type="text/javascript">initTabs();</script>
</body>
</html>
END
}

sub textBox {
    my $self = shift;
    my $text = shift;
    my $title = shift;
    
    my $TABLE;
    $TABLE .= "<table cellspacing=\"0\" class=\"text\">";
    $TABLE .= "<tr><th>$title</th></tr>" if $title;
    $TABLE .= "<tr><td>$text</td></tr>" if $text;
    $TABLE .= "</table>";
}

sub vertBox {
    my $self = shift;
    my $title = shift;
    
    my $TABLE;
    $TABLE .= "<table cellspacing=\"0\" class=\"text\">";
    $TABLE .= "<tr><th colspan=\"2\">$title</th></tr>" if $title;
    
    for( my $row = 0; $row < (@_ - 1) ; $row += 2 ) {
        $TABLE .= "<tr><td class=\"vh\">" . $_[$row] . "</td><td>" . $self->_commify($_[$row+1]) . "</td></tr>";
    }
    
    $TABLE .= "</table>";
}

sub jsTab {
    my $self = shift;
    my $section = shift;
    $section = $self->tameText($section);
    return <<END;
<script type="text/javascript">
toggleTab('$section');
</script>   

END
}

sub tameText {
    my $self = shift;
    my $text = shift;
    
    my $tamed = HTML::Entities::encode_entities(lc $text);
    $tamed =~ s/[^\w]/_/g;
    
    return $tamed;
}

sub actorLink {
    my $self = shift;
    my $id = shift || 0;
    my $single = shift;
    my $tab = shift;
    
    $single = 0 if $self->{collapse};
    my $name = $self->{index}->actorname($id);
    my $color = $self->{raid}{$id} && $self->{raid}{$id}{class};
    
    #$tab = $tab ? "#" . $self->tameText($tab) : "";
    $tab = "";
    $name ||= "";
    $color ||= "Mob";
    $color =~ s/\s//g;
    
    if( $id || (defined $id && $id eq "0") ) {
        my $group = $self->{grouper}->group($id);
        if( $group && !$single ) {
            return sprintf 
                "<a href=\"group_%s.html%s\" class=\"actor color%s\">%s</a>", 
                $self->tameText($self->{grouper}->captain($group)), 
                $tab,
                $color, 
                HTML::Entities::encode_entities($name);
        } else {
            return sprintf 
                "<a href=\"actor_%s.html%s\" class=\"actor color%s\">%s%s</a>", 
                $self->tameText($id), 
                $tab,
                $color, 
                HTML::Entities::encode_entities($name), 
                ( $group && $single ? " #" . $self->{grouper}->number($id) : "" );
        }
    } else {
        return HTML::Entities::encode_entities($name);
    }
}

sub spellLink {
    my $self = shift;
    my $id = shift;
    my $tab = shift;
    
    my ($name, $rank) = $self->{index}->spellname($id);
    $tab = $tab ? "#" . $self->tameText($tab) : "";

#    if( $id && $id =~ /^[0-9]+$/ ) {
#        return sprintf 
#            "<a href=\"spell_%s.html%s\" rel=\"spell=%s\" class=\"spell\">%s</a>%s", 
#            $id, 
#            $tab,
#            $id, 
#            HTML::Entities::encode_entities($name),
#            $rank ? " ($rank)" : "";
#    } elsif( $id ) {
#        return sprintf 
#            "<a href=\"spell_%s.html%s\" class=\"spell\">%s</a>", 
#            $id, 
#            $tab,
#            HTML::Entities::encode_entities($name);
#    } else {
        return HTML::Entities::encode_entities($name);
#    }
}

sub tip {
    my ($self, $short, $long) = @_;
    
    my $id = ++ $self->{tid};
    
    if( $long ) {
        $long =~ s/"/&quot;/g;
        return sprintf '<span id="tip%d" class="tip" title="%s">%s</span>', $id, $self->_commify($long), $self->_commify($short);
    } else {
        return $short || "";
    }
}

sub alink {
    my ($self, $loc, $text, $params) = @_;
    $params ||= {};
    
    return sprintf '<a href="%s" %s>%s</a>', $loc, join( " ", map { "$_=\"$params->{$_}\"" } keys %$params ), $text;
}

sub timespan {
    my ( $self, $start, $end, $zero, $diff, $use_range ) = @_;
    
    $start -= $zero;
    $end -= $zero;
    
    $diff = defined $diff ? $diff : $end - $start;
    my $text = sprintf( "%02d:%02d", $diff / 60, $diff % 60 );
    
    if( $use_range ) {
        $text .= sprintf( " (%02d:%02d &ndash; %02d:%02d)", $start / 60, $start % 60, $end / 60, $end % 60 );
    }
    
    return $text;
}

sub _commify {
    my $text = pop;
    
    return $text if $text =~ /(?:January|February|March|April|May|June|July|August|September|October|November|December)/;
    
    my $commas = sub { my $t = shift; 1 while $t =~ s/^(-?\d+)(\d{3})/$1,$2/; $t };
    $text =~ s/(^|[\s\(\>])([0-9]+)($|[\s\)\<])/ $1 . $commas->($2) . $3 /eg;
    return $text;
}

1;
