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

package Stasis::DB;

use strict;
use warnings;
use DBI;
use Carp;

my @line_fields = qw(t action actor actor_name actor_relationship target target_name target_relationship);

my @extra_fields = qw(absorbed amount auratype blocked critical crushing environmentaltype extraamount extraspellid extraspellname glancing misstype resisted spellid spellname extraspellschool school spellschool powertype);

my @result_fields = qw(t action actor_relationship target_relationship absorbed amount auratype blocked critical crushing environmentaltype extraamount extraspellid extraspellname glancing misstype resisted spellid spellname extraspellschool school spellschool powertype actor actor_name target target_name);

sub new {
    my $class = shift;
    my %params = @_;
    
    croak("No DB specified") unless $params{db};
    bless {
        db => $params{db},
        rollback => 0,
        actor_cache => {},
        readline_sth => undef,
    }, $class;
}

sub create {
    my $self = shift;
    
    my $dbh = $self->_dbh();
    eval {
        $self->_begin();
        
        $dbh->do( "DROP TABLE IF EXISTS line" );
        $dbh->do( "DROP TABLE IF EXISTS actor" );
        
        $dbh->do(
            "CREATE TABLE `line` (
              `line_id`             INTEGER PRIMARY KEY,
              `t`                   REAL,
              `action`              TEXT NOT NULL,
              `actor_id`            INTEGER NOT NULL DEFAULT 0,
              `actor_relationship`  INTEGER NOT NULL DEFAULT 0,
              `target_id`           INTEGER NOT NULL DEFAULT 0,
              `target_relationship` INTEGER NOT NULL DEFAULT 0,
              `absorbed`            INTEGER DEFAULT NULL,
              `amount`              INTEGER DEFAULT NULL,
              `auratype`            TEXT DEFAULT NULL,
              `blocked`             INTEGER DEFAULT NULL,
              `critical`            INTEGER DEFAULT NULL,
              `crushing`            INTEGER DEFAULT NULL,
              `environmentaltype`   TEXT DEFAULT NULL,
              `extraamount`         INTEGER DEFAULT NULL,
              `extraspellid`        INTEGER DEFAULT NULL,
              `extraspellname`      TEXT DEFAULT NULL,
              `glancing`            INTEGER DEFAULT NULL,
              `misstype`            TEXT DEFAULT NULL,
              `resisted`            INTEGER DEFAULT NULL,
              `spellid`             INTEGER DEFAULT NULL,
              `spellname`           TEXT DEFAULT NULL,
              `extraspellschool`    INTEGER DEFAULT NULL,
              `school`              INTEGER DEFAULT NULL,
              `spellschool`         INTEGER DEFAULT NULL,
              `powertype`           TEXT DEFAULT NULL
            )"
        ) or die;
        
        $dbh->do(
            "CREATE TABLE actor (
                `actor_id`          INTEGER PRIMARY KEY,
                
                `actor_guid`        TEXT NOT NULL UNIQUE,
                `actor_name`        TEXT NOT NULL
            )"
        ) or die;
        
        $self->{actor_cache} = {};
        $self->{rollback} = 0;
        $self->{readline_sth} = undef;
        $self->{readline_result} = undef;
    }; if( $@ ) {
        my $err = $@;
        eval { $self->_rollback(); };
        croak "Error creating data file: $err";
    }
}

sub finish {
    my $self = shift;
    $self->_commit();
}

sub addLine {
    my $self = shift;
    my $seq = shift;
    my $event = shift;
    
    if( $seq =~ /^[0-9]+$/ && $event ) {
        my $dbh = $self->_dbh();
        eval {
            my $sth;
            
            # Prepare the list of columns.
            my %col = (
                line_id => $seq + 1,
                t => $event->{t},
                action => $event->{action},
                actor_id => $self->_actor_need( $event->{actor}, $event->{actor_name} ),
                actor_relationship => $event->{actor_relationship},
                target_id => $self->_actor_need( $event->{target}, $event->{target_name} ),
                target_relationship => $event->{target_relationship},
            );
            
            # Add extra columns.
            foreach (@extra_fields) {
                $col{$_} = defined $event->{$_} ? $event->{$_} : 0;
            }
            
            # Insert the line itself
            $sth = $dbh->prepare( sprintf "INSERT INTO line (%s) VALUES (%s);", (join ",", keys %col), (join ",", map { "?" } keys %col ) );
            $sth->execute( values %col );
            $sth->finish;
            undef $sth;
        }; if( $@ ) {
            my $err = $@;
            eval { $self->_rollback(); };
            croak "error saving parsed log: $err";
        }
    } else {
        croak "bad input";
    }
}

sub line {
    my $self = shift;
    my $startLine = shift;
    my $endLine = shift;
    
    # Maybe change our saved sth
    if( $startLine ) {
        if( $self->{readline_sth} ) {
            $self->{readline_sth}->finish;
            undef $self->{readline_sth};
        }
        
        my $dbh = $self->_dbh();
        $self->{readline_sth} = $dbh->prepare( sprintf "SELECT line.t - 1, line.action, line.actor_relationship, line.target_relationship, line.absorbed, line.amount, line.auratype, line.blocked, line.critical, line.crushing, line.environmentaltype, line.extraamount, line.extraspellid, line.extraspellname, line.glancing, line.misstype, line.resisted, line.spellid, line.spellname, line.extraspellschool, line.school, line.spellschool, line.powertype, actor1.actor_guid AS actor, actor1.actor_name AS actor_name, actor2.actor_guid AS target, actor2.actor_name AS target_name FROM line LEFT JOIN actor AS actor1 ON line.actor_id = actor1.actor_id LEFT JOIN actor AS actor2 ON line.target_id = actor2.actor_id %s ORDER BY line.line_id;", $endLine ? "WHERE line.line_id >= ? AND line.line_id <= ?" : "" );
        
        my @exec = $endLine ? ($startLine, $endLine) : ();
        
        $self->{readline_sth}->execute( @exec );
        
        # Set up result bindings.
        my %result;
        @result{@result_fields} = ();
        $self->{readline_sth}->bind_columns( map { \$result{$_} } @result_fields );
        $self->{readline_result} = \%result;
        
        # Just return nothing.
        return undef;
    } elsif( $self->{readline_sth} ) {
        # Read more from the result set.
        if( $self->{readline_sth}->fetch ) {
            my %result;
            foreach (@line_fields) {
                $result{$_} = $self->{readline_result}{$_};
            }
            
            if( !$result{actor} ) {
                $result{actor} = 0;
                $result{actor_name} = "";
            }
            
            if( !$result{target} ) {
                $result{target} = 0;
                $result{target_name} = "";
            }
            
            foreach (@extra_fields) {
                $result{$_} = $self->{readline_result}{$_} if defined $self->{readline_result}{$_};
            }
            
            return \%result;
        } else {
            # Reached the end.
            $self->{readline_sth}->finish;
            $self->{readline_sth} = undef;
            $self->{readline_result} = undef;
            return undef;
        }
    } else {
        croak "line() without line(start, end)";
    }
}

sub disconnect {
    my $self = shift;

    if( $self->{dbh} ) {
        $self->{dbh}->disconnect;
        undef $self->{dbh};
    }
}

sub _actor_need {
    my $self = shift;
    my $guid = shift;
    my $name = shift;
    
    # Return 0 if the guid is not set.
    if( !$guid ) {
        return 0;
    }
    
    # Otherwise look for the correct actor_id :
    if( $self->{actor_cache}{$guid} ) {
        return $self->{actor_cache}{$guid};
    } else {
        my $dbh = $self->_dbh();
        my $sth = $dbh->prepare( "SELECT actor_id FROM actor WHERE actor_guid = ?" );
        $sth->execute( $guid );

        my $result = $sth->fetchrow_hashref;
        $sth->finish;
        undef $sth;

        if( $result ) {
            $self->{actor_cache}{$guid} = $result->{actor_id};
            return $self->{actor_cache}{$guid};
        } elsif( $name ) {
            # Name was set
            $sth = $dbh->prepare( "INSERT INTO actor (actor_guid, actor_name) VALUES (?, ?);");
            $sth->execute( $guid, $name );
            $sth->finish;
            undef $sth;
            
            $self->{actor_cache}{$guid} = $dbh->func('last_insert_rowid');
            return $self->{actor_cache}{$guid};
        } else {
            # Name was not set... the caller expected this thing to be found.
            croak "actor not found: $guid";
        }
    }
}

sub _begin {
    my $self = shift;
    my $dbh = $self->_dbh();
    $dbh->do( "BEGIN" );
}

sub _rollback {
    my $self = shift;
    
    $self->{rollback} = 1;
    my $dbh = $self->_dbh();
    $dbh->do( "ROLLBACK" );
    $self->disconnect();
}

sub _commit {
    my $self = shift;
    my $dbh = $self->_dbh();
    $dbh->do( "COMMIT" );
}

sub _dbh {
    my $self = shift;
    
    if( $self->{rollback} ) {
        croak "db object has been rolled back, create a new one";
    }
    
    $self->{dbh} ||= DBI->connect( "dbi:SQLite:" . $self->{db}, undef, undef, {RaiseError=>1} ) or croak "Cannot read local database: $DBI::errstr";
    return $self->{dbh};
}

1;
