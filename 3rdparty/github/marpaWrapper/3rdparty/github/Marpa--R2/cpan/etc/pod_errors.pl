use 5.010001;

use Pod::Simple::Checker;
use Data::Dumper;

print Dumper(Pod::Simple::Checker->filter(shift)->any_errata_seen)
