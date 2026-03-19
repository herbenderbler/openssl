#! /usr/bin/env perl
# Copyright 2025 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the Apache License 2.0 (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

use strict;
use warnings;

use OpenSSL::Test qw/:DEFAULT bldtop_dir srctop_file indir cmd run/;

setup("test_find_doc_nits");

plan tests => 1;

# Regression test for find-doc-nits -a scanning .t and .pl files for env vars.
# ECSTRESS is only referenced in test/recipes/99-test_ecstress.t and is not
# in openssl-env.pod, so it will appear in the undocumented list as long as
# we scan Perl files.
indir(bldtop_dir() => sub {
    my $script = srctop_file("util", "find-doc-nits");
    my @out = run(cmd([ $^X, $script, "-a" ]), capture => 1);
    my $output = join("", @out);
    ok($output =~ /ECSTRESS/m,
       "find-doc-nits -a scans .t and .pl files for environment variables");
});
