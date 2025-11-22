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

package Stasis::CLI::Writer;

use strict;
use warnings;

use File::Path ();
use POSIX qw/floor/;

sub new {
    my ( $class, %params ) = @_;
    
    my $self = bless {
        base     => $params{base},
        server   => $params{server},
        template => $params{template} || "nno-:short:-:start:",
        fork     => $params{fork},
        workers  => {},
        written  => [],
    }, $class;
    
    return $self;
}

sub set {
    my ( $self, %params ) = @_;

    foreach my $key qw/boss raid exts index collapse/ {
        $self->{$key} = $params{$key};
    }
}

sub fill_template {
    my ( $self ) = @_;
    my ( $boss, $raid, $exts, $index, $collapse ) = map { $self->{$_} } qw/boss raid exts index collapse/;

    my $players =
      grep { $exts->{Presence}{actors}{$_} }
      grep { $raid->{$_} && $raid->{$_}{class} && $raid->{$_}{class} ne "Pet" } keys %$raid;
    
    my $players_rounded = $players <= 13 ? 10 : 25;    # kind of arbitrary

    my @time = localtime( $boss->{start} || 0 );
    my %tmp = (
        short  => $boss->{short},
        start  => floor( $boss->{start} || 0 ),
        end    => floor( $boss->{end} || 0 ),
        kill   => $boss->{kill},
        raid   => $players,
        rraid  => $players_rounded,
        month  => sprintf( "%02d", $time[4] + 1 ),
        day    => sprintf( "%02d", $time[3] ),
        year   => sprintf( "%04d", $time[5] + 1900 ),
        hour   => sprintf( "%02d", $time[2] ),
        minute => sprintf( "%02d", $time[1] ),
        second => sprintf( "%02d", $time[0] ),
    );

    my $template = $self->{template};
    $template =~ s/:(\w+):/ defined $tmp{$1} ? $tmp{$1} : ":$1:" /eg;
    return $template;
}

sub written_dirs {
    my ( $self ) = @_;
    
    if( $self->{fork} ) {
        while( ( my $cpid = wait ) != -1 ) {
            die "Child exited with status: $?" if $?;
            
            if( my $fh = delete $self->{workers}{$cpid} ) {
                my $h;

                while( my $r = <$fh> ) {
                    $h->{$1} = $2 if $r =~ /^(\w+):(.*)$/;
                }

                close $fh;
                push @{ $self->{written} }, $h;
            }
        }

        if( keys %{ $self->{workers} } ) {
            warn "Children still unaccounted for: " . join ", ", keys %{ $self->{workers} };
        }
    }

    return @{ $self->{written} };
}

sub write_dir {
    my ( $self ) = @_;
    
    if( $self->{fork} ) {
        pipe my $pfh, my $cfh or die "could not open connected pipes: $!";
        
        my $cpid = fork;
        
        if( !defined $cpid ) {
            die "could not fork: $!";
        } elsif( $cpid ) {
            # parent
            close $cfh;
            $self->{workers}{$cpid} = $pfh;
            return;
        } else {
            # child
            close $pfh;
        
            # so we don't end up killing our siblings via DESTROY
            $self->{fork} = 0;
            
            eval {
                my $h = $self->_write_dir;
                print $cfh join "\n", map { $_ . ":" . $h->{$_} } keys %$h;
                print $cfh "\n";
                exit 0;
            };
            
            exit 1;
        }
    } else {
        # not forking
        push @{ $self->{written} }, $self->_write_dir(@_);
    }
}

sub _write_dir {
    my ( $self ) = @_;
    my ( $boss, $raid, $exts, $index, $collapse ) = map { $self->{$_} } qw/boss raid exts index collapse/;
    
    my $dname_suffix = $self->fill_template;
    my $dname = sprintf "%s/%s", $self->{base}, $dname_suffix;

    if( -d $self->{base} ) {
        File::Path::rmtree( $dname ) if -d $dname;
        File::Path::mkpath( $dname );
    } else {
        die "not a directory: " . $self->{base};
    }

    # Group actors.
    my $grouper = Stasis::ActorGroup->new;
    $grouper->run( $raid, $exts, $index );

    # Initialize Pages with these parameters.
    my %page_init = (
        server   => $self->{server},
        dirname  => $dname_suffix,
        name     => $boss->{long},
        short    => $boss->{short},
        raid     => $raid,
        ext      => $exts,
        collapse => $collapse,
        grouper  => $grouper,
        index    => $index,
    );

    # Write the index.
    my $charter = Stasis::Page::Chart->new( %page_init );

    my ( $chart_xml, $chart_html ) = $charter->page;
    open my $fh_chartpage, ">$dname/index.html" or die;
    print $fh_chartpage $chart_html;
    close $fh_chartpage;

    # Write the actor files.
    my $ap = Stasis::Page::Actor->new( %page_init );

    foreach my $actor ( keys %{ $exts->{Presence}{actors} } ) {
        # Respect $collapse.
        next if $collapse && $grouper->group( $actor );

        my $id = lc $actor;
        $id = Stasis::PageMaker->tameText( $id );

        open my $fh_actorpage, sprintf ">$dname/actor_%s.html", $id or die;
        print $fh_actorpage $ap->page( $actor );
        close $fh_actorpage;
    }

    # Write the group files.
    foreach my $group ( @{ $grouper->{groups} } ) {
        my $id = lc $grouper->captain( $group );
        $id = Stasis::PageMaker->tameText( $id );

        open my $fh_grouppage, sprintf ">$dname/group_%s.html", $id or die;
        print $fh_grouppage $ap->page( $grouper->captain( $group ), 1 );
        close $fh_grouppage;
    }

    # Write the environment file.
    open my $env_page, ">$dname/actor_0.html" or die;
    print $env_page $ap->page( 0 );
    close $env_page;

    # Write the spell files.
    my $sp = Stasis::Page::Spell->new( %page_init );
    
    foreach my $spell ( keys %{ $index->{spells} } ) {
        my $id = lc $spell;
        $id = Stasis::PageMaker->tameText( $id );

        open my $fh_spellpage, sprintf ">$dname/spell_%s.html", $id or die;
        print $fh_spellpage $sp->page( $spell );
        close $fh_spellpage;
    }
    
    # Write death clips.
    my $lc = Stasis::Page::LogClip->new( %page_init );

    while( my ( $kactor, $vactor ) = each( %{ $exts->{Death}{actors} } ) ) {
        my $id = lc $kactor;
        $id = Stasis::PageMaker->tameText( $id );

        my $dn = 0;
        foreach my $death ( @$vactor ) {
            $lc->clear;
            foreach my $event ( @{ $death->{autopsy} } ) {
                $lc->add( $event->{event}, hp => $event->{hp}, t => $event->{t} );
            }

            open my $fh_deathpage, sprintf ">$dname/death_%s_%d.json", $id, ++$dn or die;
            print $fh_deathpage $lc->json;
            close $fh_deathpage;
        }
    }

    # Write the data.xml file.
    open my $fh_dataxml, ">$dname/data.xml" or die;
    print $fh_dataxml $chart_xml;
    close $fh_dataxml;

    # Return a hash describing what we just wrote.
    my ( $rdamage, $rstart, $rend ) = ( 0, $exts->{Presence}->presence );
    $rdamage = $1 if( $chart_xml =~ /<raid[^>]+dmg="(\d+)"/ );

    return {
        dname  => $dname_suffix,
        short  => $boss->{short},
        long   => $boss->{long},
        damage => $rdamage,
        start  => $rstart,
        end    => $rend,
    };
}

1;
