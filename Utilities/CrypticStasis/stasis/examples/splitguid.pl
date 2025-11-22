use strict;
use warnings;
use lib 'lib';
use Stasis::MobUtil qw/splitguid/;

print map { "$_\n" } map { join " ", splitguid $_ } @ARGV;
