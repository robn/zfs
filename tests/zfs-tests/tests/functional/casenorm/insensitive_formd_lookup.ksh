#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/casenorm/casenorm.kshlib

# DESCRIPTION:
# For the filesystem with casesensitivity=insensitive, normalization=formD,
# check that lookup succeeds using any name form.
#
# STRATEGY:
# For each c/n name form:
# 1. Create file with given c/n name form.
# 2. Check that lookup succeeds for any other c/n name form.

verify_runnable "global"

function cleanup
{
	destroy_testfs
}

log_onexit cleanup
log_assert "CI-UN FS: lookup succeeds using any name form"

create_testfs "-o casesensitivity=insensitive -o normalization=formD"

for spec1 in $SPECS_ALL ; do
	log_must casenorm create $spec1 $TESTDIR
	for spec2 in $SPECS_ALL ; do
		log_must casenorm lookup $spec2 $TESTDIR
	done
	log_must casenorm delete $spec1 $TESTDIR
done

destroy_testfs

log_pass "CI-UN FS: lookup succeeds using any name form"
