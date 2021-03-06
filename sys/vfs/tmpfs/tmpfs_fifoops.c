/*	$NetBSD: tmpfs_fifoops.c,v 1.5 2005/12/11 12:24:29 christos Exp $	*/

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Julio M. Merino Vidal, developed as part of Google's Summer of Code
 * 2005 program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * tmpfs vnode interface for named pipes.
 */

#include <sys/kernel.h>
#include <sys/param.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_object.h>

#include <vfs/fifofs/fifo.h>
#include <vfs/tmpfs/tmpfs.h>
#include <vfs/tmpfs/tmpfs_vnops.h>

/* --------------------------------------------------------------------- */

static int
tmpfs_fifo_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp;
	struct tmpfs_node *node;

	vp = ap->a_vp;
	node = VP_TO_TMPFS_NODE(vp);

	switch (ap->a_kn->kn_filter){
	case EVFILT_READ:
		node->tn_status |= TMPFS_NODE_ACCESSED;
		break;
	case EVFILT_WRITE:
		node->tn_status |= TMPFS_NODE_MODIFIED;
		break;
	}

	return fifo_vnode_vops.vop_kqfilter(ap);
}

/* --------------------------------------------------------------------- */

static int
tmpfs_fifo_close(struct vop_close_args *ap)
{
	struct tmpfs_node *node;
	node = VP_TO_TMPFS_NODE(ap->a_vp);
	node->tn_status |= TMPFS_NODE_ACCESSED;

	tmpfs_update(ap->a_vp);
	return fifo_vnode_vops.vop_close(ap);
}

/*
 * vnode operations vector used for fifos stored in a tmpfs file system.
 */
struct vop_ops tmpfs_fifo_vops = {
	.vop_default =			fifo_vnoperate,
	.vop_close =			tmpfs_fifo_close,
	.vop_reclaim =			tmpfs_reclaim,
	.vop_access =			tmpfs_access,
	.vop_getattr =			tmpfs_getattr,
	.vop_setattr =			tmpfs_setattr,
	.vop_kqfilter =			tmpfs_fifo_kqfilter,
};
