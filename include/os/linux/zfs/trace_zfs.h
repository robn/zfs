// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

#ifndef _ZFS_TRACE_ZFS_H
#define	_ZFS_TRACE_ZFS_H

#include <zfs/multilist.h>
#include <zfs/arc_impl.h>
#include <zfs/vdev_impl.h>
#include <zfs/zio.h>
#include <zfs/dbuf.h>
#include <zfs/dmu_objset.h>
#include <zfs/dsl_dataset.h>
#include <zfs/dmu_tx.h>
#include <zfs/dnode.h>
#include <zfs/zfs_znode.h>
#include <zfs/zil_impl.h>
#include <zfs/zrlock.h>

#include <spl/trace.h>
#include <zfs/trace_acl.h>
#include <zfs/trace_arc.h>
#include <zfs/trace_dbgmsg.h>
#include <zfs/trace_dbuf.h>
#include <zfs/trace_dmu.h>
#include <zfs/trace_dnode.h>
#include <zfs/trace_multilist.h>
#include <zfs/trace_rrwlock.h>
#include <zfs/trace_txg.h>
#include <zfs/trace_vdev.h>
#include <zfs/trace_zil.h>
#include <zfs/trace_zio.h>
#include <zfs/trace_zrlock.h>

#endif
