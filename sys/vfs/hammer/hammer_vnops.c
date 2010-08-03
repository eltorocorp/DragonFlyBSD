/*
 * Copyright (c) 2007-2008 The DragonFly Project.  All rights reserved.
 * 
 * This code is derived from software contributed to The DragonFly Project
 * by Matthew Dillon <dillon@backplane.com>
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of The DragonFly Project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific, prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * $DragonFly: src/sys/vfs/hammer/hammer_vnops.c,v 1.102 2008/10/16 17:24:16 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/namecache.h>
#include <sys/vnode.h>
#include <sys/lockf.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <sys/dirent.h>
#include <sys/file.h>
#include <vm/vm_extern.h>
#include <vfs/fifofs/fifo.h>

#include <sys/mplock2.h>

#include "hammer.h"

/*
 * USERFS VNOPS
 */
/*static int hammer_vop_vnoperate(struct vop_generic_args *);*/
static int hammer_vop_fsync(struct vop_fsync_args *);
static int hammer_vop_read(struct vop_read_args *);
static int hammer_vop_write(struct vop_write_args *);
static int hammer_vop_access(struct vop_access_args *);
static int hammer_vop_advlock(struct vop_advlock_args *);
static int hammer_vop_close(struct vop_close_args *);
static int hammer_vop_ncreate(struct vop_ncreate_args *);
static int hammer_vop_getattr(struct vop_getattr_args *);
static int hammer_vop_nresolve(struct vop_nresolve_args *);
static int hammer_vop_nlookupdotdot(struct vop_nlookupdotdot_args *);
static int hammer_vop_nlink(struct vop_nlink_args *);
static int hammer_vop_nmkdir(struct vop_nmkdir_args *);
static int hammer_vop_nmknod(struct vop_nmknod_args *);
static int hammer_vop_open(struct vop_open_args *);
static int hammer_vop_print(struct vop_print_args *);
static int hammer_vop_readdir(struct vop_readdir_args *);
static int hammer_vop_readlink(struct vop_readlink_args *);
static int hammer_vop_nremove(struct vop_nremove_args *);
static int hammer_vop_nrename(struct vop_nrename_args *);
static int hammer_vop_nrmdir(struct vop_nrmdir_args *);
static int hammer_vop_markatime(struct vop_markatime_args *);
static int hammer_vop_setattr(struct vop_setattr_args *);
static int hammer_vop_strategy(struct vop_strategy_args *);
static int hammer_vop_bmap(struct vop_bmap_args *ap);
static int hammer_vop_nsymlink(struct vop_nsymlink_args *);
static int hammer_vop_nwhiteout(struct vop_nwhiteout_args *);
static int hammer_vop_ioctl(struct vop_ioctl_args *);
static int hammer_vop_mountctl(struct vop_mountctl_args *);
static int hammer_vop_kqfilter (struct vop_kqfilter_args *);

static int hammer_vop_fifoclose (struct vop_close_args *);
static int hammer_vop_fiforead (struct vop_read_args *);
static int hammer_vop_fifowrite (struct vop_write_args *);
static int hammer_vop_fifokqfilter (struct vop_kqfilter_args *);

struct vop_ops hammer_vnode_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		hammer_vop_fsync,
	.vop_getpages =		vop_stdgetpages,
	.vop_putpages =		vop_stdputpages,
	.vop_read =		hammer_vop_read,
	.vop_write =		hammer_vop_write,
	.vop_access =		hammer_vop_access,
	.vop_advlock =		hammer_vop_advlock,
	.vop_close =		hammer_vop_close,
	.vop_ncreate =		hammer_vop_ncreate,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_nresolve =		hammer_vop_nresolve,
	.vop_nlookupdotdot =	hammer_vop_nlookupdotdot,
	.vop_nlink =		hammer_vop_nlink,
	.vop_nmkdir =		hammer_vop_nmkdir,
	.vop_nmknod =		hammer_vop_nmknod,
	.vop_open =		hammer_vop_open,
	.vop_pathconf =		vop_stdpathconf,
	.vop_print =		hammer_vop_print,
	.vop_readdir =		hammer_vop_readdir,
	.vop_readlink =		hammer_vop_readlink,
	.vop_nremove =		hammer_vop_nremove,
	.vop_nrename =		hammer_vop_nrename,
	.vop_nrmdir =		hammer_vop_nrmdir,
	.vop_markatime = 	hammer_vop_markatime,
	.vop_setattr =		hammer_vop_setattr,
	.vop_bmap =		hammer_vop_bmap,
	.vop_strategy =		hammer_vop_strategy,
	.vop_nsymlink =		hammer_vop_nsymlink,
	.vop_nwhiteout =	hammer_vop_nwhiteout,
	.vop_ioctl =		hammer_vop_ioctl,
	.vop_mountctl =		hammer_vop_mountctl,
	.vop_kqfilter =		hammer_vop_kqfilter
};

struct vop_ops hammer_spec_vops = {
	.vop_default =		vop_defaultop,
	.vop_fsync =		hammer_vop_fsync,
	.vop_read =		vop_stdnoread,
	.vop_write =		vop_stdnowrite,
	.vop_access =		hammer_vop_access,
	.vop_close =		hammer_vop_close,
	.vop_markatime = 	hammer_vop_markatime,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_setattr =		hammer_vop_setattr
};

struct vop_ops hammer_fifo_vops = {
	.vop_default =		fifo_vnoperate,
	.vop_fsync =		hammer_vop_fsync,
	.vop_read =		hammer_vop_fiforead,
	.vop_write =		hammer_vop_fifowrite,
	.vop_access =		hammer_vop_access,
	.vop_close =		hammer_vop_fifoclose,
	.vop_markatime = 	hammer_vop_markatime,
	.vop_getattr =		hammer_vop_getattr,
	.vop_inactive =		hammer_vop_inactive,
	.vop_reclaim =		hammer_vop_reclaim,
	.vop_setattr =		hammer_vop_setattr,
	.vop_kqfilter =		hammer_vop_fifokqfilter
};

static __inline
void
hammer_knote(struct vnode *vp, int flags)
{
	if (flags)
		KNOTE(&vp->v_pollinfo.vpi_kqinfo.ki_note, flags);
}

#ifdef DEBUG_TRUNCATE
struct hammer_inode *HammerTruncIp;
#endif

static int hammer_dounlink(hammer_transaction_t trans, struct nchandle *nch,
			   struct vnode *dvp, struct ucred *cred,
			   int flags, int isdir);
static int hammer_vop_strategy_read(struct vop_strategy_args *ap);
static int hammer_vop_strategy_write(struct vop_strategy_args *ap);

#if 0
static
int
hammer_vop_vnoperate(struct vop_generic_args *)
{
	return (VOCALL(&hammer_vnode_vops, ap));
}
#endif

/*
 * hammer_vop_fsync { vp, waitfor }
 *
 * fsync() an inode to disk and wait for it to be completely committed
 * such that the information would not be undone if a crash occured after
 * return.
 *
 * NOTE: HAMMER's fsync()'s are going to remain expensive until we implement
 *	 a REDO log.  A sysctl is provided to relax HAMMER's fsync()
 *	 operation.
 *
 *	 Ultimately the combination of a REDO log and use of fast storage
 *	 to front-end cluster caches will make fsync fast, but it aint
 *	 here yet.  And, in anycase, we need real transactional
 *	 all-or-nothing features which are not restricted to a single file.
 */
static
int
hammer_vop_fsync(struct vop_fsync_args *ap)
{
	hammer_inode_t ip = VTOI(ap->a_vp);
	hammer_mount_t hmp = ip->hmp;
	int waitfor = ap->a_waitfor;
	int mode;

	/*
	 * Fsync rule relaxation (default is either full synchronous flush
	 * or REDO semantics with synchronous flush).
	 */
	if (ap->a_flags & VOP_FSYNC_SYSCALL) {
		switch(hammer_fsync_mode) {
		case 0:
mode0:
			/* no REDO, full synchronous flush */
			goto skip;
		case 1:
mode1:
			/* no REDO, full asynchronous flush */
			if (waitfor == MNT_WAIT)
				waitfor = MNT_NOWAIT;
			goto skip;
		case 2:
			/* REDO semantics, synchronous flush */
			if (hmp->version < HAMMER_VOL_VERSION_FOUR)
				goto mode0;
			mode = HAMMER_FLUSH_UNDOS_AUTO;
			break;
		case 3:
			/* REDO semantics, relaxed asynchronous flush */
			if (hmp->version < HAMMER_VOL_VERSION_FOUR)
				goto mode1;
			mode = HAMMER_FLUSH_UNDOS_RELAXED;
			if (waitfor == MNT_WAIT)
				waitfor = MNT_NOWAIT;
			break;
		case 4:
			/* ignore the fsync() system call */
			return(0);
		default:
			/* we have to do something */
			mode = HAMMER_FLUSH_UNDOS_RELAXED;
			if (waitfor == MNT_WAIT)
				waitfor = MNT_NOWAIT;
			break;
		}

		/*
		 * Fast fsync only needs to flush the UNDO/REDO fifo if
		 * HAMMER_INODE_REDO is non-zero and the only modifications
		 * made to the file are write or write-extends.
		 */
		if ((ip->flags & HAMMER_INODE_REDO) &&
		    (ip->flags & HAMMER_INODE_MODMASK_NOREDO) == 0
		) {
			++hammer_count_fsyncs;
			hammer_flusher_flush_undos(hmp, mode);
			ip->redo_count = 0;
			return(0);
		}

		/*
		 * REDO is enabled by fsync(), the idea being we really only
		 * want to lay down REDO records when programs are using
		 * fsync() heavily.  The first fsync() on the file starts
		 * the gravy train going and later fsync()s keep it hot by
		 * resetting the redo_count.
		 *
		 * We weren't running REDOs before now so we have to fall
		 * through and do a full fsync of what we have.
		 */
		if (hmp->version >= HAMMER_VOL_VERSION_FOUR &&
		    (hmp->flags & HAMMER_MOUNT_REDO_RECOVERY_RUN) == 0) {
			ip->flags |= HAMMER_INODE_REDO;
			ip->redo_count = 0;
		}
	}
skip:

	/*
	 * Do a full flush sequence.
	 */
	++hammer_count_fsyncs;
	vfsync(ap->a_vp, waitfor, 1, NULL, NULL);
	hammer_flush_inode(ip, HAMMER_FLUSH_SIGNAL);
	if (waitfor == MNT_WAIT) {
		vn_unlock(ap->a_vp);
		hammer_wait_inode(ip);
		vn_lock(ap->a_vp, LK_EXCLUSIVE | LK_RETRY);
	}
	return (ip->error);
}

/*
 * hammer_vop_read { vp, uio, ioflag, cred }
 *
 * MPALMOSTSAFE
 */
static
int
hammer_vop_read(struct vop_read_args *ap)
{
	struct hammer_transaction trans;
	hammer_inode_t ip;
	off_t offset;
	struct buf *bp;
	struct uio *uio;
	int error;
	int n;
	int seqcount;
	int ioseqcount;
	int blksize;
	int got_mplock;
	int bigread;

	if (ap->a_vp->v_type != VREG)
		return (EINVAL);
	ip = VTOI(ap->a_vp);
	error = 0;
	uio = ap->a_uio;

	/*
	 * Allow the UIO's size to override the sequential heuristic.
	 */
	blksize = hammer_blocksize(uio->uio_offset);
	seqcount = (uio->uio_resid + (BKVASIZE - 1)) / BKVASIZE;
	ioseqcount = (ap->a_ioflag >> 16);
	if (seqcount < ioseqcount)
		seqcount = ioseqcount;

	/*
	 * Temporary hack until more of HAMMER can be made MPSAFE.
	 */
#ifdef SMP
	if (curthread->td_mpcount) {
		got_mplock = -1;
		hammer_start_transaction(&trans, ip->hmp);
	} else {
		got_mplock = 0;
	}
#else
	hammer_start_transaction(&trans, ip->hmp);
	got_mplock = -1;
#endif

	/*
	 * If reading or writing a huge amount of data we have to break
	 * atomicy and allow the operation to be interrupted by a signal
	 * or it can DOS the machine.
	 */
	bigread = (uio->uio_resid > 100 * 1024 * 1024);

	/*
	 * Access the data typically in HAMMER_BUFSIZE blocks via the
	 * buffer cache, but HAMMER may use a variable block size based
	 * on the offset.
	 *
	 * XXX Temporary hack, delay the start transaction while we remain
	 *     MPSAFE.  NOTE: ino_data.size cannot change while vnode is
	 *     locked-shared.
	 */
	while (uio->uio_resid > 0 && uio->uio_offset < ip->ino_data.size) {
		int64_t base_offset;
		int64_t file_limit;

		blksize = hammer_blocksize(uio->uio_offset);
		offset = (int)uio->uio_offset & (blksize - 1);
		base_offset = uio->uio_offset - offset;

		if (bigread && (error = hammer_signal_check(ip->hmp)) != 0)
			break;

		/*
		 * MPSAFE
		 */
		bp = getcacheblk(ap->a_vp, base_offset);
		if (bp) {
			error = 0;
			goto skip;
		}

		/*
		 * MPUNSAFE
		 */
		if (got_mplock == 0) {
			got_mplock = 1;
			get_mplock();
			hammer_start_transaction(&trans, ip->hmp);
		}

		if (hammer_cluster_enable) {
			/*
			 * Use file_limit to prevent cluster_read() from
			 * creating buffers of the wrong block size past
			 * the demarc.
			 */
			file_limit = ip->ino_data.size;
			if (base_offset < HAMMER_XDEMARC &&
			    file_limit > HAMMER_XDEMARC) {
				file_limit = HAMMER_XDEMARC;
			}
			error = cluster_read(ap->a_vp,
					     file_limit, base_offset,
					     blksize, MAXPHYS,
					     seqcount, &bp);
		} else {
			error = bread(ap->a_vp, base_offset, blksize, &bp);
		}
		if (error) {
			brelse(bp);
			break;
		}
skip:

		/* bp->b_flags |= B_CLUSTEROK; temporarily disabled */
		n = blksize - offset;
		if (n > uio->uio_resid)
			n = uio->uio_resid;
		if (n > ip->ino_data.size - uio->uio_offset)
			n = (int)(ip->ino_data.size - uio->uio_offset);
		error = uiomove((char *)bp->b_data + offset, n, uio);

		/* data has a lower priority then meta-data */
		bp->b_flags |= B_AGE;
		bqrelse(bp);
		if (error)
			break;
		hammer_stats_file_read += n;
	}

	/*
	 * XXX only update the atime if we had to get the MP lock.
	 * XXX hack hack hack, fixme.
	 */
	if (got_mplock) {
		if ((ip->flags & HAMMER_INODE_RO) == 0 &&
		    (ip->hmp->mp->mnt_flag & MNT_NOATIME) == 0) {
			ip->ino_data.atime = trans.time;
			hammer_modify_inode(&trans, ip, HAMMER_INODE_ATIME);
		}
		hammer_done_transaction(&trans);
		if (got_mplock > 0)
			rel_mplock();
	}
	return (error);
}

/*
 * hammer_vop_write { vp, uio, ioflag, cred }
 */
static
int
hammer_vop_write(struct vop_write_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	hammer_mount_t hmp;
	struct uio *uio;
	int offset;
	off_t base_offset;
	struct buf *bp;
	int kflags;
	int error;
	int n;
	int flags;
	int seqcount;
	int bigwrite;

	if (ap->a_vp->v_type != VREG)
		return (EINVAL);
	ip = VTOI(ap->a_vp);
	hmp = ip->hmp;
	error = 0;
	kflags = 0;
	seqcount = ap->a_ioflag >> 16;

	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, hmp);
	uio = ap->a_uio;

	/*
	 * Check append mode
	 */
	if (ap->a_ioflag & IO_APPEND)
		uio->uio_offset = ip->ino_data.size;

	/*
	 * Check for illegal write offsets.  Valid range is 0...2^63-1.
	 *
	 * NOTE: the base_off assignment is required to work around what
	 * I consider to be a GCC-4 optimization bug.
	 */
	if (uio->uio_offset < 0) {
		hammer_done_transaction(&trans);
		return (EFBIG);
	}
	base_offset = uio->uio_offset + uio->uio_resid;	/* work around gcc-4 */
	if (uio->uio_resid > 0 && base_offset <= uio->uio_offset) {
		hammer_done_transaction(&trans);
		return (EFBIG);
	}

	/*
	 * If reading or writing a huge amount of data we have to break
	 * atomicy and allow the operation to be interrupted by a signal
	 * or it can DOS the machine.
	 *
	 * Preset redo_count so we stop generating REDOs earlier if the
	 * limit is exceeded.
	 */
	bigwrite = (uio->uio_resid > 100 * 1024 * 1024);
	if ((ip->flags & HAMMER_INODE_REDO) &&
	    ip->redo_count < hammer_limit_redo) {
		ip->redo_count += uio->uio_resid;
	}

	/*
	 * Access the data typically in HAMMER_BUFSIZE blocks via the
	 * buffer cache, but HAMMER may use a variable block size based
	 * on the offset.
	 */
	while (uio->uio_resid > 0) {
		int fixsize = 0;
		int blksize;
		int blkmask;
		int trivial;
		int endofblk;
		off_t nsize;

		if ((error = hammer_checkspace(hmp, HAMMER_CHKSPC_WRITE)) != 0)
			break;
		if (bigwrite && (error = hammer_signal_check(hmp)) != 0)
			break;

		blksize = hammer_blocksize(uio->uio_offset);

		/*
		 * Do not allow HAMMER to blow out the buffer cache.  Very
		 * large UIOs can lockout other processes due to bwillwrite()
		 * mechanics.
		 *
		 * The hammer inode is not locked during these operations.
		 * The vnode is locked which can interfere with the pageout
		 * daemon for non-UIO_NOCOPY writes but should not interfere
		 * with the buffer cache.  Even so, we cannot afford to
		 * allow the pageout daemon to build up too many dirty buffer
		 * cache buffers.
		 *
		 * Only call this if we aren't being recursively called from
		 * a virtual disk device (vn), else we may deadlock.
		 */
		if ((ap->a_ioflag & IO_RECURSE) == 0)
			bwillwrite(blksize);

		/*
		 * Control the number of pending records associated with
		 * this inode.  If too many have accumulated start a
		 * flush.  Try to maintain a pipeline with the flusher.
		 */
		if (ip->rsv_recs >= hammer_limit_inode_recs) {
			hammer_flush_inode(ip, HAMMER_FLUSH_SIGNAL);
		}
		if (ip->rsv_recs >= hammer_limit_inode_recs * 2) {
			while (ip->rsv_recs >= hammer_limit_inode_recs) {
				tsleep(&ip->rsv_recs, 0, "hmrwww", hz);
			}
			hammer_flush_inode(ip, HAMMER_FLUSH_SIGNAL);
		}

#if 0
		/*
		 * Do not allow HAMMER to blow out system memory by
		 * accumulating too many records.   Records are so well
		 * decoupled from the buffer cache that it is possible
		 * for userland to push data out to the media via
		 * direct-write, but build up the records queued to the
		 * backend faster then the backend can flush them out.
		 * HAMMER has hit its write limit but the frontend has
		 * no pushback to slow it down.
		 */
		if (hmp->rsv_recs > hammer_limit_recs / 2) {
			/*
			 * Get the inode on the flush list
			 */
			if (ip->rsv_recs >= 64)
				hammer_flush_inode(ip, HAMMER_FLUSH_SIGNAL);
			else if (ip->rsv_recs >= 16)
				hammer_flush_inode(ip, 0);

			/*
			 * Keep the flusher going if the system keeps
			 * queueing records.
			 */
			delta = hmp->count_newrecords -
				hmp->last_newrecords;
			if (delta < 0 || delta > hammer_limit_recs / 2) {
				hmp->last_newrecords = hmp->count_newrecords;
				hammer_sync_hmp(hmp, MNT_NOWAIT);
			}

			/*
			 * If we have gotten behind start slowing
			 * down the writers.
			 */
			delta = (hmp->rsv_recs - hammer_limit_recs) *
				hz / hammer_limit_recs;
			if (delta > 0)
				tsleep(&trans, 0, "hmrslo", delta);
		}
#endif

		/*
		 * Calculate the blocksize at the current offset and figure
		 * out how much we can actually write.
		 */
		blkmask = blksize - 1;
		offset = (int)uio->uio_offset & blkmask;
		base_offset = uio->uio_offset & ~(int64_t)blkmask;
		n = blksize - offset;
		if (n > uio->uio_resid) {
			n = uio->uio_resid;
			endofblk = 0;
		} else {
			endofblk = 1;
		}
		nsize = uio->uio_offset + n;
		if (nsize > ip->ino_data.size) {
			if (uio->uio_offset > ip->ino_data.size)
				trivial = 0;
			else
				trivial = 1;
			nvextendbuf(ap->a_vp,
				    ip->ino_data.size,
				    nsize,
				    hammer_blocksize(ip->ino_data.size),
				    hammer_blocksize(nsize),
				    hammer_blockoff(ip->ino_data.size),
				    hammer_blockoff(nsize),
				    trivial);
			fixsize = 1;
			kflags |= NOTE_EXTEND;
		}

		if (uio->uio_segflg == UIO_NOCOPY) {
			/*
			 * Issuing a write with the same data backing the
			 * buffer.  Instantiate the buffer to collect the
			 * backing vm pages, then read-in any missing bits.
			 *
			 * This case is used by vop_stdputpages().
			 */
			bp = getblk(ap->a_vp, base_offset,
				    blksize, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0) {
				bqrelse(bp);
				error = bread(ap->a_vp, base_offset,
					      blksize, &bp);
			}
		} else if (offset == 0 && uio->uio_resid >= blksize) {
			/*
			 * Even though we are entirely overwriting the buffer
			 * we may still have to zero it out to avoid a 
			 * mmap/write visibility issue.
			 */
			bp = getblk(ap->a_vp, base_offset, blksize, GETBLK_BHEAVY, 0);
			if ((bp->b_flags & B_CACHE) == 0)
				vfs_bio_clrbuf(bp);
		} else if (base_offset >= ip->ino_data.size) {
			/*
			 * If the base offset of the buffer is beyond the
			 * file EOF, we don't have to issue a read.
			 */
			bp = getblk(ap->a_vp, base_offset,
				    blksize, GETBLK_BHEAVY, 0);
			vfs_bio_clrbuf(bp);
		} else {
			/*
			 * Partial overwrite, read in any missing bits then
			 * replace the portion being written.
			 */
			error = bread(ap->a_vp, base_offset, blksize, &bp);
			if (error == 0)
				bheavy(bp);
		}
		if (error == 0)
			error = uiomove(bp->b_data + offset, n, uio);

		/*
		 * Generate REDO records if enabled and redo_count will not
		 * exceeded the limit.
		 *
		 * If redo_count exceeds the limit we stop generating records
		 * and clear HAMMER_INODE_REDO.  This will cause the next
		 * fsync() to do a full meta-data sync instead of just an
		 * UNDO/REDO fifo update.
		 *
		 * When clearing HAMMER_INODE_REDO any pre-existing REDOs
		 * will still be tracked.  The tracks will be terminated
		 * when the related meta-data (including possible data
		 * modifications which are not tracked via REDO) is
		 * flushed.
		 */
		if ((ip->flags & HAMMER_INODE_REDO) && error == 0) {
			if (ip->redo_count < hammer_limit_redo) {
				bp->b_flags |= B_VFSFLAG1;
				error = hammer_generate_redo(&trans, ip,
						     base_offset + offset,
						     HAMMER_REDO_WRITE,
						     bp->b_data + offset,
						     (size_t)n);
			} else {
				ip->flags &= ~HAMMER_INODE_REDO;
			}
		}

		/*
		 * If we screwed up we have to undo any VM size changes we
		 * made.
		 */
		if (error) {
			brelse(bp);
			if (fixsize) {
				nvtruncbuf(ap->a_vp, ip->ino_data.size,
					  hammer_blocksize(ip->ino_data.size),
					  hammer_blockoff(ip->ino_data.size));
			}
			break;
		}
		kflags |= NOTE_WRITE;
		hammer_stats_file_write += n;
		/* bp->b_flags |= B_CLUSTEROK; temporarily disabled */
		if (ip->ino_data.size < uio->uio_offset) {
			ip->ino_data.size = uio->uio_offset;
			flags = HAMMER_INODE_SDIRTY;
		} else {
			flags = 0;
		}
		ip->ino_data.mtime = trans.time;
		flags |= HAMMER_INODE_MTIME | HAMMER_INODE_BUFS;
		hammer_modify_inode(&trans, ip, flags);

		/*
		 * Once we dirty the buffer any cached zone-X offset
		 * becomes invalid.  HAMMER NOTE: no-history mode cannot 
		 * allow overwriting over the same data sector unless
		 * we provide UNDOs for the old data, which we don't.
		 */
		bp->b_bio2.bio_offset = NOOFFSET;

		/*
		 * Final buffer disposition.
		 *
		 * Because meta-data updates are deferred, HAMMER is
		 * especially sensitive to excessive bdwrite()s because
		 * the I/O stream is not broken up by disk reads.  So the
		 * buffer cache simply cannot keep up.
		 *
		 * WARNING!  blksize is variable.  cluster_write() is
		 *	     expected to not blow up if it encounters
		 *	     buffers that do not match the passed blksize.
		 *
		 * NOTE!  Hammer shouldn't need to bawrite()/cluster_write().
		 *	  The ip->rsv_recs check should burst-flush the data.
		 *	  If we queue it immediately the buf could be left
		 *	  locked on the device queue for a very long time.
		 *
		 * NOTE!  To avoid degenerate stalls due to mismatched block
		 *	  sizes we only honor IO_DIRECT on the write which
		 *	  abuts the end of the buffer.  However, we must
		 *	  honor IO_SYNC in case someone is silly enough to
		 *	  configure a HAMMER file as swap, or when HAMMER
		 *	  is serving NFS (for commits).  Ick ick.
		 */
		bp->b_flags |= B_AGE;
		if (ap->a_ioflag & IO_SYNC) {
			bwrite(bp);
		} else if ((ap->a_ioflag & IO_DIRECT) && endofblk) {
			bawrite(bp);
		} else {
#if 0
		if (offset + n == blksize) {
			if (hammer_cluster_enable == 0 ||
			    (ap->a_vp->v_mount->mnt_flag & MNT_NOCLUSTERW)) {
				bawrite(bp);
			} else {
				cluster_write(bp, ip->ino_data.size,
					      blksize, seqcount);
			}
		} else {
#endif
			bdwrite(bp);
		}
	}
	hammer_done_transaction(&trans);
	hammer_knote(ap->a_vp, kflags);
	return (error);
}

/*
 * hammer_vop_access { vp, mode, cred }
 */
static
int
hammer_vop_access(struct vop_access_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);
	uid_t uid;
	gid_t gid;
	int error;

	++hammer_stats_file_iopsr;
	uid = hammer_to_unix_xid(&ip->ino_data.uid);
	gid = hammer_to_unix_xid(&ip->ino_data.gid);

	error = vop_helper_access(ap, uid, gid, ip->ino_data.mode,
				  ip->ino_data.uflags);
	return (error);
}

/*
 * hammer_vop_advlock { vp, id, op, fl, flags }
 */
static
int
hammer_vop_advlock(struct vop_advlock_args *ap)
{
	hammer_inode_t ip = VTOI(ap->a_vp);

	return (lf_advlock(ap, &ip->advlock, ip->ino_data.size));
}

/*
 * hammer_vop_close { vp, fflag }
 *
 * We can only sync-on-close for normal closes.
 */
static
int
hammer_vop_close(struct vop_close_args *ap)
{
#if 0
	struct vnode *vp = ap->a_vp;
	hammer_inode_t ip = VTOI(vp);
	int waitfor;
	if (ip->flags & (HAMMER_INODE_CLOSESYNC|HAMMER_INODE_CLOSEASYNC)) {
		if (vn_islocked(vp) == LK_EXCLUSIVE &&
		    (vp->v_flag & (VINACTIVE|VRECLAIMED)) == 0) {
			if (ip->flags & HAMMER_INODE_CLOSESYNC)
				waitfor = MNT_WAIT;
			else
				waitfor = MNT_NOWAIT;
			ip->flags &= ~(HAMMER_INODE_CLOSESYNC |
				       HAMMER_INODE_CLOSEASYNC);
			VOP_FSYNC(vp, MNT_NOWAIT, waitfor);
		}
	}
#endif
	return (vop_stdclose(ap));
}

/*
 * hammer_vop_ncreate { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_ncreate(struct vop_ncreate_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp, HAMMER_CHKSPC_CREATE)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsw;

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced and shared-locked to prevent
	 * it from being moved to the flusher.
	 */
	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred,
				    dip, nch->ncp->nc_name, nch->ncp->nc_nlen,
				    NULL, &nip);
	if (error) {
		hkprintf("hammer_create_inode error %d\n", error);
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip,
					nch->ncp->nc_name, nch->ncp->nc_nlen,
					nip);
	if (error)
		hkprintf("hammer_ip_add_directory error %d\n", error);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
	} else {
		error = hammer_get_vnode(nip, ap->a_vpp);
		hammer_done_transaction(&trans);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
		hammer_knote(ap->a_dvp, NOTE_WRITE);
	}
	return (error);
}

/*
 * hammer_vop_getattr { vp, vap }
 *
 * Retrieve an inode's attribute information.  When accessing inodes
 * historically we fake the atime field to ensure consistent results.
 * The atime field is stored in the B-Tree element and allowed to be
 * updated without cycling the element.
 *
 * MPSAFE
 */
static
int
hammer_vop_getattr(struct vop_getattr_args *ap)
{
	struct hammer_inode *ip = VTOI(ap->a_vp);
	struct vattr *vap = ap->a_vap;

	/*
	 * We want the fsid to be different when accessing a filesystem
	 * with different as-of's so programs like diff don't think
	 * the files are the same.
	 *
	 * We also want the fsid to be the same when comparing snapshots,
	 * or when comparing mirrors (which might be backed by different
	 * physical devices).  HAMMER fsids are based on the PFS's
	 * shared_uuid field.
	 *
	 * XXX there is a chance of collision here.  The va_fsid reported
	 * by stat is different from the more involved fsid used in the
	 * mount structure.
	 */
	++hammer_stats_file_iopsr;
	hammer_lock_sh(&ip->lock);
	vap->va_fsid = ip->pfsm->fsid_udev ^ (u_int32_t)ip->obj_asof ^
		       (u_int32_t)(ip->obj_asof >> 32);

	vap->va_fileid = ip->ino_leaf.base.obj_id;
	vap->va_mode = ip->ino_data.mode;
	vap->va_nlink = ip->ino_data.nlinks;
	vap->va_uid = hammer_to_unix_xid(&ip->ino_data.uid);
	vap->va_gid = hammer_to_unix_xid(&ip->ino_data.gid);
	vap->va_rmajor = 0;
	vap->va_rminor = 0;
	vap->va_size = ip->ino_data.size;

	/*
	 * Special case for @@PFS softlinks.  The actual size of the
	 * expanded softlink is "@@0x%016llx:%05d" == 26 bytes.
	 * or for MAX_TID is    "@@-1:%05d" == 10 bytes.
	 */
	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_SOFTLINK &&
	    ip->ino_data.size == 10 &&
	    ip->obj_asof == HAMMER_MAX_TID &&
	    ip->obj_localization == 0 &&
	    strncmp(ip->ino_data.ext.symlink, "@@PFS", 5) == 0) {
		    if (ip->pfsm->pfsd.mirror_flags & HAMMER_PFSD_SLAVE)
			    vap->va_size = 26;
		    else
			    vap->va_size = 10;
	}

	/*
	 * We must provide a consistent atime and mtime for snapshots
	 * so people can do a 'tar cf - ... | md5' on them and get
	 * consistent results.
	 */
	if (ip->flags & HAMMER_INODE_RO) {
		hammer_time_to_timespec(ip->ino_data.ctime, &vap->va_atime);
		hammer_time_to_timespec(ip->ino_data.ctime, &vap->va_mtime);
	} else {
		hammer_time_to_timespec(ip->ino_data.atime, &vap->va_atime);
		hammer_time_to_timespec(ip->ino_data.mtime, &vap->va_mtime);
	}
	hammer_time_to_timespec(ip->ino_data.ctime, &vap->va_ctime);
	vap->va_flags = ip->ino_data.uflags;
	vap->va_gen = 1;	/* hammer inums are unique for all time */
	vap->va_blocksize = HAMMER_BUFSIZE;
	if (ip->ino_data.size >= HAMMER_XDEMARC) {
		vap->va_bytes = (ip->ino_data.size + HAMMER_XBUFMASK64) &
				~HAMMER_XBUFMASK64;
	} else if (ip->ino_data.size > HAMMER_BUFSIZE / 2) {
		vap->va_bytes = (ip->ino_data.size + HAMMER_BUFMASK64) &
				~HAMMER_BUFMASK64;
	} else {
		vap->va_bytes = (ip->ino_data.size + 15) & ~15;
	}

	vap->va_type = hammer_get_vnode_type(ip->ino_data.obj_type);
	vap->va_filerev = 0; 	/* XXX */
	vap->va_uid_uuid = ip->ino_data.uid;
	vap->va_gid_uuid = ip->ino_data.gid;
	vap->va_fsid_uuid = ip->hmp->fsid;
	vap->va_vaflags = VA_UID_UUID_VALID | VA_GID_UUID_VALID |
			  VA_FSID_UUID_VALID;

	switch (ip->ino_data.obj_type) {
	case HAMMER_OBJTYPE_CDEV:
	case HAMMER_OBJTYPE_BDEV:
		vap->va_rmajor = ip->ino_data.rmajor;
		vap->va_rminor = ip->ino_data.rminor;
		break;
	default:
		break;
	}
	hammer_unlock(&ip->lock);
	return(0);
}

/*
 * hammer_vop_nresolve { nch, dvp, cred }
 *
 * Locate the requested directory entry.
 */
static
int
hammer_vop_nresolve(struct vop_nresolve_args *ap)
{
	struct hammer_transaction trans;
	struct namecache *ncp;
	hammer_inode_t dip;
	hammer_inode_t ip;
	hammer_tid_t asof;
	struct hammer_cursor cursor;
	struct vnode *vp;
	int64_t namekey;
	int error;
	int i;
	int nlen;
	int flags;
	int ispfs;
	int64_t obj_id;
	u_int32_t localization;
	u_int32_t max_iterations;

	/*
	 * Misc initialization, plus handle as-of name extensions.  Look for
	 * the '@@' extension.  Note that as-of files and directories cannot
	 * be modified.
	 */
	dip = VTOI(ap->a_dvp);
	ncp = ap->a_nch->ncp;
	asof = dip->obj_asof;
	localization = dip->obj_localization;	/* for code consistency */
	nlen = ncp->nc_nlen;
	flags = dip->flags & HAMMER_INODE_RO;
	ispfs = 0;

	hammer_simple_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsr;

	for (i = 0; i < nlen; ++i) {
		if (ncp->nc_name[i] == '@' && ncp->nc_name[i+1] == '@') {
			error = hammer_str_to_tid(ncp->nc_name + i + 2,
						  &ispfs, &asof, &localization);
			if (error != 0) {
				i = nlen;
				break;
			}
			if (asof != HAMMER_MAX_TID)
				flags |= HAMMER_INODE_RO;
			break;
		}
	}
	nlen = i;

	/*
	 * If this is a PFS softlink we dive into the PFS
	 */
	if (ispfs && nlen == 0) {
		ip = hammer_get_inode(&trans, dip, HAMMER_OBJID_ROOT,
				      asof, localization,
				      flags, &error);
		if (error == 0) {
			error = hammer_get_vnode(ip, &vp);
			hammer_rel_inode(ip, 0);
		} else {
			vp = NULL;
		}
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
		goto done;
	}

	/*
	 * If there is no path component the time extension is relative to dip.
	 * e.g. "fubar/@@<snapshot>"
	 *
	 * "." is handled by the kernel, but ".@@<snapshot>" is not.
	 * e.g. "fubar/.@@<snapshot>"
	 *
	 * ".." is handled by the kernel.  We do not currently handle
	 * "..@<snapshot>".
	 */
	if (nlen == 0 || (nlen == 1 && ncp->nc_name[0] == '.')) {
		ip = hammer_get_inode(&trans, dip, dip->obj_id,
				      asof, dip->obj_localization,
				      flags, &error);
		if (error == 0) {
			error = hammer_get_vnode(ip, &vp);
			hammer_rel_inode(ip, 0);
		} else {
			vp = NULL;
		}
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
		goto done;
	}

	/*
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	namekey = hammer_directory_namekey(dip, ncp->nc_name, nlen,
					   &max_iterations);

	error = hammer_init_cursor(&trans, &cursor, &dip->cache[1], dip);
	cursor.key_beg.localization = dip->obj_localization +
				      hammer_dir_localization(dip);
        cursor.key_beg.obj_id = dip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key += max_iterations;
	cursor.asof = asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	obj_id = 0;
	localization = HAMMER_DEF_LOCALIZATION;

	if (error == 0) {
		error = hammer_ip_first(&cursor);
		while (error == 0) {
			error = hammer_ip_resolve_data(&cursor);
			if (error)
				break;
			if (nlen == cursor.leaf->data_len - HAMMER_ENTRY_NAME_OFF &&
			    bcmp(ncp->nc_name, cursor.data->entry.name, nlen) == 0) {
				obj_id = cursor.data->entry.obj_id;
				localization = cursor.data->entry.localization;
				break;
			}
			error = hammer_ip_next(&cursor);
		}
	}
	hammer_done_cursor(&cursor);

	/*
	 * Lookup the obj_id.  This should always succeed.  If it does not
	 * the filesystem may be damaged and we return a dummy inode.
	 */
	if (error == 0) {
		ip = hammer_get_inode(&trans, dip, obj_id,
				      asof, localization,
				      flags, &error);
		if (error == ENOENT) {
			kprintf("HAMMER: WARNING: Missing "
				"inode for dirent \"%s\"\n"
				"\tobj_id = %016llx, asof=%016llx, lo=%08x\n",
				ncp->nc_name,
				(long long)obj_id, (long long)asof,
				localization);
			error = 0;
			ip = hammer_get_dummy_inode(&trans, dip, obj_id,
						    asof, localization,
						    flags, &error);
		}
		if (error == 0) {
			error = hammer_get_vnode(ip, &vp);
			hammer_rel_inode(ip, 0);
		} else {
			vp = NULL;
		}
		if (error == 0) {
			vn_unlock(vp);
			cache_setvp(ap->a_nch, vp);
			vrele(vp);
		}
	} else if (error == ENOENT) {
		cache_setvp(ap->a_nch, NULL);
	}
done:
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nlookupdotdot { dvp, vpp, cred }
 *
 * Locate the parent directory of a directory vnode.
 *
 * dvp is referenced but not locked.  *vpp must be returned referenced and
 * locked.  A parent_obj_id of 0 does not necessarily indicate that we are
 * at the root, instead it could indicate that the directory we were in was
 * removed.
 *
 * NOTE: as-of sequences are not linked into the directory structure.  If
 * we are at the root with a different asof then the mount point, reload
 * the same directory with the mount point's asof.   I'm not sure what this
 * will do to NFS.  We encode ASOF stamps in NFS file handles so it might not
 * get confused, but it hasn't been tested.
 */
static
int
hammer_vop_nlookupdotdot(struct vop_nlookupdotdot_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *ip;
	int64_t parent_obj_id;
	u_int32_t parent_obj_localization;
	hammer_tid_t asof;
	int error;

	dip = VTOI(ap->a_dvp);
	asof = dip->obj_asof;

	/*
	 * Whos are parent?  This could be the root of a pseudo-filesystem
	 * whos parent is in another localization domain.
	 */
	parent_obj_id = dip->ino_data.parent_obj_id;
	if (dip->obj_id == HAMMER_OBJID_ROOT)
		parent_obj_localization = dip->ino_data.ext.obj.parent_obj_localization;
	else
		parent_obj_localization = dip->obj_localization;

	if (parent_obj_id == 0) {
		if (dip->obj_id == HAMMER_OBJID_ROOT &&
		   asof != dip->hmp->asof) {
			parent_obj_id = dip->obj_id;
			asof = dip->hmp->asof;
			*ap->a_fakename = kmalloc(19, M_TEMP, M_WAITOK);
			ksnprintf(*ap->a_fakename, 19, "0x%016llx",
				  (long long)dip->obj_asof);
		} else {
			*ap->a_vpp = NULL;
			return ENOENT;
		}
	}

	hammer_simple_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsr;

	ip = hammer_get_inode(&trans, dip, parent_obj_id,
			      asof, parent_obj_localization,
			      dip->flags, &error);
	if (ip) {
		error = hammer_get_vnode(ip, ap->a_vpp);
		hammer_rel_inode(ip, 0);
	} else {
		*ap->a_vpp = NULL;
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nlink { nch, dvp, vp, cred }
 */
static
int
hammer_vop_nlink(struct vop_nlink_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *ip;
	struct nchandle *nch;
	int error;

	if (ap->a_dvp->v_mount != ap->a_vp->v_mount)	
		return(EXDEV);

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);
	ip = VTOI(ap->a_vp);

	if (dip->obj_localization != ip->obj_localization)
		return(EXDEV);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp, HAMMER_CHKSPC_CREATE)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsw;

	/*
	 * Add the filesystem object to the directory.  Note that neither
	 * dip nor ip are referenced or locked, but their vnodes are
	 * referenced.  This function will bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip,
					nch->ncp->nc_name, nch->ncp->nc_nlen,
					ip);

	/*
	 * Finish up.
	 */
	if (error == 0) {
		cache_setunresolved(nch);
		cache_setvp(nch, ap->a_vp);
	}
	hammer_done_transaction(&trans);
	hammer_knote(ap->a_vp, NOTE_LINK);
	hammer_knote(ap->a_dvp, NOTE_WRITE);
	return (error);
}

/*
 * hammer_vop_nmkdir { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_nmkdir(struct vop_nmkdir_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp, HAMMER_CHKSPC_CREATE)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsw;

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */
	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred,
				    dip, nch->ncp->nc_name, nch->ncp->nc_nlen,
				    NULL, &nip);
	if (error) {
		hkprintf("hammer_mkdir error %d\n", error);
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}
	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip,
					nch->ncp->nc_name, nch->ncp->nc_nlen,
					nip);
	if (error)
		hkprintf("hammer_mkdir (add) error %d\n", error);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		*ap->a_vpp = NULL;
	} else {
		error = hammer_get_vnode(nip, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
	}
	hammer_done_transaction(&trans);
	if (error == 0)
		hammer_knote(ap->a_dvp, NOTE_WRITE | NOTE_LINK);
	return (error);
}

/*
 * hammer_vop_nmknod { nch, dvp, vpp, cred, vap }
 *
 * The operating system has already ensured that the directory entry
 * does not exist and done all appropriate namespace locking.
 */
static
int
hammer_vop_nmknod(struct vop_nmknod_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	int error;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp, HAMMER_CHKSPC_CREATE)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsw;

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 *
	 * If mknod specifies a directory a pseudo-fs is created.
	 */
	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred,
				    dip, nch->ncp->nc_name, nch->ncp->nc_nlen,
				    NULL, &nip);
	if (error) {
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add the new filesystem object to the directory.  This will also
	 * bump the inode's link count.
	 */
	error = hammer_ip_add_directory(&trans, dip,
					nch->ncp->nc_name, nch->ncp->nc_nlen,
					nip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		*ap->a_vpp = NULL;
	} else {
		error = hammer_get_vnode(nip, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
		}
	}
	hammer_done_transaction(&trans);
	if (error == 0)
		hammer_knote(ap->a_dvp, NOTE_WRITE);
	return (error);
}

/*
 * hammer_vop_open { vp, mode, cred, fp }
 */
static
int
hammer_vop_open(struct vop_open_args *ap)
{
	hammer_inode_t ip;

	++hammer_stats_file_iopsr;
	ip = VTOI(ap->a_vp);

	if ((ap->a_mode & FWRITE) && (ip->flags & HAMMER_INODE_RO))
		return (EROFS);
	return(vop_stdopen(ap));
}

/*
 * hammer_vop_print { vp }
 */
static
int
hammer_vop_print(struct vop_print_args *ap)
{
	return EOPNOTSUPP;
}

/*
 * hammer_vop_readdir { vp, uio, cred, *eofflag, *ncookies, off_t **cookies }
 */
static
int
hammer_vop_readdir(struct vop_readdir_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_cursor cursor;
	struct hammer_inode *ip;
	struct uio *uio;
	hammer_base_elm_t base;
	int error;
	int cookie_index;
	int ncookies;
	off_t *cookies;
	off_t saveoff;
	int r;
	int dtype;

	++hammer_stats_file_iopsr;
	ip = VTOI(ap->a_vp);
	uio = ap->a_uio;
	saveoff = uio->uio_offset;

	if (ap->a_ncookies) {
		ncookies = uio->uio_resid / 16 + 1;
		if (ncookies > 1024)
			ncookies = 1024;
		cookies = kmalloc(ncookies * sizeof(off_t), M_TEMP, M_WAITOK);
		cookie_index = 0;
	} else {
		ncookies = -1;
		cookies = NULL;
		cookie_index = 0;
	}

	hammer_simple_transaction(&trans, ip->hmp);

	/*
	 * Handle artificial entries
	 *
	 * It should be noted that the minimum value for a directory
	 * hash key on-media is 0x0000000100000000, so we can use anything
	 * less then that to represent our 'special' key space.
	 */
	error = 0;
	if (saveoff == 0) {
		r = vop_write_dirent(&error, uio, ip->obj_id, DT_DIR, 1, ".");
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			goto done;
	}
	if (saveoff == 1) {
		if (ip->ino_data.parent_obj_id) {
			r = vop_write_dirent(&error, uio,
					     ip->ino_data.parent_obj_id,
					     DT_DIR, 2, "..");
		} else {
			r = vop_write_dirent(&error, uio,
					     ip->obj_id, DT_DIR, 2, "..");
		}
		if (r)
			goto done;
		if (cookies)
			cookies[cookie_index] = saveoff;
		++saveoff;
		++cookie_index;
		if (cookie_index == ncookies)
			goto done;
	}

	/*
	 * Key range (begin and end inclusive) to scan.  Directory keys
	 * directly translate to a 64 bit 'seek' position.
	 */
	hammer_init_cursor(&trans, &cursor, &ip->cache[1], ip);
	cursor.key_beg.localization = ip->obj_localization +
				      hammer_dir_localization(ip);
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = saveoff;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key = HAMMER_MAX_KEY;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	error = hammer_ip_first(&cursor);

	while (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			break;
		base = &cursor.leaf->base;
		saveoff = base->key;
		KKASSERT(cursor.leaf->data_len > HAMMER_ENTRY_NAME_OFF);

		if (base->obj_id != ip->obj_id)
			panic("readdir: bad record at %p", cursor.node);

		/*
		 * Convert pseudo-filesystems into softlinks
		 */
		dtype = hammer_get_dtype(cursor.leaf->base.obj_type);
		r = vop_write_dirent(
			     &error, uio, cursor.data->entry.obj_id,
			     dtype,
			     cursor.leaf->data_len - HAMMER_ENTRY_NAME_OFF ,
			     (void *)cursor.data->entry.name);
		if (r)
			break;
		++saveoff;
		if (cookies)
			cookies[cookie_index] = base->key;
		++cookie_index;
		if (cookie_index == ncookies)
			break;
		error = hammer_ip_next(&cursor);
	}
	hammer_done_cursor(&cursor);

done:
	hammer_done_transaction(&trans);

	if (ap->a_eofflag)
		*ap->a_eofflag = (error == ENOENT);
	uio->uio_offset = saveoff;
	if (error && cookie_index == 0) {
		if (error == ENOENT)
			error = 0;
		if (cookies) {
			kfree(cookies, M_TEMP);
			*ap->a_ncookies = 0;
			*ap->a_cookies = NULL;
		}
	} else {
		if (error == ENOENT)
			error = 0;
		if (cookies) {
			*ap->a_ncookies = cookie_index;
			*ap->a_cookies = cookies;
		}
	}
	return(error);
}

/*
 * hammer_vop_readlink { vp, uio, cred }
 */
static
int
hammer_vop_readlink(struct vop_readlink_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_cursor cursor;
	struct hammer_inode *ip;
	char buf[32];
	u_int32_t localization;
	hammer_pseudofs_inmem_t pfsm;
	int error;

	ip = VTOI(ap->a_vp);

	/*
	 * Shortcut if the symlink data was stuffed into ino_data.
	 *
	 * Also expand special "@@PFS%05d" softlinks (expansion only
	 * occurs for non-historical (current) accesses made from the
	 * primary filesystem).
	 */
	if (ip->ino_data.size <= HAMMER_INODE_BASESYMLEN) {
		char *ptr;
		int bytes;

		ptr = ip->ino_data.ext.symlink;
		bytes = (int)ip->ino_data.size;
		if (bytes == 10 &&
		    ip->obj_asof == HAMMER_MAX_TID &&
		    ip->obj_localization == 0 &&
		    strncmp(ptr, "@@PFS", 5) == 0) {
			hammer_simple_transaction(&trans, ip->hmp);
			bcopy(ptr + 5, buf, 5);
			buf[5] = 0;
			localization = strtoul(buf, NULL, 10) << 16;
			pfsm = hammer_load_pseudofs(&trans, localization,
						    &error);
			if (error == 0) {
				if (pfsm->pfsd.mirror_flags &
				    HAMMER_PFSD_SLAVE) {
					/* vap->va_size == 26 */
					ksnprintf(buf, sizeof(buf),
						  "@@0x%016llx:%05d",
						  (long long)pfsm->pfsd.sync_end_tid,
						  localization >> 16);
				} else {
					/* vap->va_size == 10 */
					ksnprintf(buf, sizeof(buf),
						  "@@-1:%05d",
						  localization >> 16);
#if 0
					ksnprintf(buf, sizeof(buf),
						  "@@0x%016llx:%05d",
						  (long long)HAMMER_MAX_TID,
						  localization >> 16);
#endif
				}
				ptr = buf;
				bytes = strlen(buf);
			}
			if (pfsm)
				hammer_rel_pseudofs(trans.hmp, pfsm);
			hammer_done_transaction(&trans);
		}
		error = uiomove(ptr, bytes, ap->a_uio);
		return(error);
	}

	/*
	 * Long version
	 */
	hammer_simple_transaction(&trans, ip->hmp);
	++hammer_stats_file_iopsr;
	hammer_init_cursor(&trans, &cursor, &ip->cache[1], ip);

	/*
	 * Key range (begin and end inclusive) to scan.  Directory keys
	 * directly translate to a 64 bit 'seek' position.
	 */
	cursor.key_beg.localization = ip->obj_localization +
				      HAMMER_LOCALIZE_MISC;
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_FIX;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = HAMMER_FIXKEY_SYMLINK;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	error = hammer_ip_lookup(&cursor);
	if (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error == 0) {
			KKASSERT(cursor.leaf->data_len >=
				 HAMMER_SYMLINK_NAME_OFF);
			error = uiomove(cursor.data->symlink.name,
					cursor.leaf->data_len -
						HAMMER_SYMLINK_NAME_OFF,
					ap->a_uio);
		}
	}
	hammer_done_cursor(&cursor);
	hammer_done_transaction(&trans);
	return(error);
}

/*
 * hammer_vop_nremove { nch, dvp, cred }
 */
static
int
hammer_vop_nremove(struct vop_nremove_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	int error;

	dip = VTOI(ap->a_dvp);

	if (hammer_nohistory(dip) == 0 &&
	    (error = hammer_checkspace(dip->hmp, HAMMER_CHKSPC_REMOVE)) != 0) {
		return (error);
	}

	hammer_start_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsw;
	error = hammer_dounlink(&trans, ap->a_nch, ap->a_dvp, ap->a_cred, 0, 0);
	hammer_done_transaction(&trans);
	if (error == 0)
		hammer_knote(ap->a_dvp, NOTE_WRITE);
	return (error);
}

/*
 * hammer_vop_nrename { fnch, tnch, fdvp, tdvp, cred }
 */
static
int
hammer_vop_nrename(struct vop_nrename_args *ap)
{
	struct hammer_transaction trans;
	struct namecache *fncp;
	struct namecache *tncp;
	struct hammer_inode *fdip;
	struct hammer_inode *tdip;
	struct hammer_inode *ip;
	struct hammer_cursor cursor;
	int64_t namekey;
	u_int32_t max_iterations;
	int nlen, error;

	if (ap->a_fdvp->v_mount != ap->a_tdvp->v_mount)	
		return(EXDEV);
	if (ap->a_fdvp->v_mount != ap->a_fnch->ncp->nc_vp->v_mount)
		return(EXDEV);

	fdip = VTOI(ap->a_fdvp);
	tdip = VTOI(ap->a_tdvp);
	fncp = ap->a_fnch->ncp;
	tncp = ap->a_tnch->ncp;
	ip = VTOI(fncp->nc_vp);
	KKASSERT(ip != NULL);

	if (fdip->obj_localization != tdip->obj_localization)
		return(EXDEV);
	if (fdip->obj_localization != ip->obj_localization)
		return(EXDEV);

	if (fdip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (tdip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(fdip->hmp, HAMMER_CHKSPC_CREATE)) != 0)
		return (error);

	hammer_start_transaction(&trans, fdip->hmp);
	++hammer_stats_file_iopsw;

	/*
	 * Remove tncp from the target directory and then link ip as
	 * tncp. XXX pass trans to dounlink
	 *
	 * Force the inode sync-time to match the transaction so it is
	 * in-sync with the creation of the target directory entry.
	 */
	error = hammer_dounlink(&trans, ap->a_tnch, ap->a_tdvp,
				ap->a_cred, 0, -1);
	if (error == 0 || error == ENOENT) {
		error = hammer_ip_add_directory(&trans, tdip,
						tncp->nc_name, tncp->nc_nlen,
						ip);
		if (error == 0) {
			ip->ino_data.parent_obj_id = tdip->obj_id;
			ip->ino_data.ctime = trans.time;
			hammer_modify_inode(&trans, ip, HAMMER_INODE_DDIRTY);
		}
	}
	if (error)
		goto failed; /* XXX */

	/*
	 * Locate the record in the originating directory and remove it.
	 *
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	namekey = hammer_directory_namekey(fdip, fncp->nc_name, fncp->nc_nlen,
					   &max_iterations);
retry:
	hammer_init_cursor(&trans, &cursor, &fdip->cache[1], fdip);
	cursor.key_beg.localization = fdip->obj_localization +
				      hammer_dir_localization(fdip);
        cursor.key_beg.obj_id = fdip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key += max_iterations;
	cursor.asof = fdip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	error = hammer_ip_first(&cursor);
	while (error == 0) {
		if (hammer_ip_resolve_data(&cursor) != 0)
			break;
		nlen = cursor.leaf->data_len - HAMMER_ENTRY_NAME_OFF;
		KKASSERT(nlen > 0);
		if (fncp->nc_nlen == nlen &&
		    bcmp(fncp->nc_name, cursor.data->entry.name, nlen) == 0) {
			break;
		}
		error = hammer_ip_next(&cursor);
	}

	/*
	 * If all is ok we have to get the inode so we can adjust nlinks.
	 *
	 * WARNING: hammer_ip_del_directory() may have to terminate the
	 * cursor to avoid a recursion.  It's ok to call hammer_done_cursor()
	 * twice.
	 */
	if (error == 0)
		error = hammer_ip_del_directory(&trans, &cursor, fdip, ip);

	/*
	 * XXX A deadlock here will break rename's atomicy for the purposes
	 * of crash recovery.
	 */
	if (error == EDEADLK) {
		hammer_done_cursor(&cursor);
		goto retry;
	}

	/*
	 * Cleanup and tell the kernel that the rename succeeded.
	 */
        hammer_done_cursor(&cursor);
	if (error == 0) {
		cache_rename(ap->a_fnch, ap->a_tnch);
		hammer_knote(ap->a_fdvp, NOTE_WRITE);
		hammer_knote(ap->a_tdvp, NOTE_WRITE);
		if (ip->vp)
			hammer_knote(ip->vp, NOTE_RENAME);
	}

failed:
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nrmdir { nch, dvp, cred }
 */
static
int
hammer_vop_nrmdir(struct vop_nrmdir_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	int error;

	dip = VTOI(ap->a_dvp);

	if (hammer_nohistory(dip) == 0 &&
	    (error = hammer_checkspace(dip->hmp, HAMMER_CHKSPC_REMOVE)) != 0) {
		return (error);
	}

	hammer_start_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsw;
	error = hammer_dounlink(&trans, ap->a_nch, ap->a_dvp, ap->a_cred, 0, 1);
	hammer_done_transaction(&trans);
	if (error == 0)
		hammer_knote(ap->a_dvp, NOTE_WRITE | NOTE_LINK);
	return (error);
}

/*
 * hammer_vop_markatime { vp, cred }
 */
static
int
hammer_vop_markatime(struct vop_markatime_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;

	ip = VTOI(ap->a_vp);
	if (ap->a_vp->v_mount->mnt_flag & MNT_RDONLY)
		return (EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (ip->hmp->mp->mnt_flag & MNT_NOATIME)
		return (0);
	hammer_start_transaction(&trans, ip->hmp);
	++hammer_stats_file_iopsw;

	ip->ino_data.atime = trans.time;
	hammer_modify_inode(&trans, ip, HAMMER_INODE_ATIME);
	hammer_done_transaction(&trans);
	hammer_knote(ap->a_vp, NOTE_ATTRIB);
	return (0);
}

/*
 * hammer_vop_setattr { vp, vap, cred }
 */
static
int
hammer_vop_setattr(struct vop_setattr_args *ap)
{
	struct hammer_transaction trans;
	struct vattr *vap;
	struct hammer_inode *ip;
	int modflags;
	int error;
	int truncating;
	int blksize;
	int kflags;
#if 0
	int64_t aligned_size;
#endif
	u_int32_t flags;

	vap = ap->a_vap;
	ip = ap->a_vp->v_data;
	modflags = 0;
	kflags = 0;

	if (ap->a_vp->v_mount->mnt_flag & MNT_RDONLY)
		return(EROFS);
	if (ip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if (hammer_nohistory(ip) == 0 &&
	    (error = hammer_checkspace(ip->hmp, HAMMER_CHKSPC_REMOVE)) != 0) {
		return (error);
	}

	hammer_start_transaction(&trans, ip->hmp);
	++hammer_stats_file_iopsw;
	error = 0;

	if (vap->va_flags != VNOVAL) {
		flags = ip->ino_data.uflags;
		error = vop_helper_setattr_flags(&flags, vap->va_flags,
					 hammer_to_unix_xid(&ip->ino_data.uid),
					 ap->a_cred);
		if (error == 0) {
			if (ip->ino_data.uflags != flags) {
				ip->ino_data.uflags = flags;
				ip->ino_data.ctime = trans.time;
				modflags |= HAMMER_INODE_DDIRTY;
				kflags |= NOTE_ATTRIB;
			}
			if (ip->ino_data.uflags & (IMMUTABLE | APPEND)) {
				error = 0;
				goto done;
			}
		}
		goto done;
	}
	if (ip->ino_data.uflags & (IMMUTABLE | APPEND)) {
		error = EPERM;
		goto done;
	}
	if (vap->va_uid != (uid_t)VNOVAL || vap->va_gid != (gid_t)VNOVAL) {
		mode_t cur_mode = ip->ino_data.mode;
		uid_t cur_uid = hammer_to_unix_xid(&ip->ino_data.uid);
		gid_t cur_gid = hammer_to_unix_xid(&ip->ino_data.gid);
		uuid_t uuid_uid;
		uuid_t uuid_gid;

		error = vop_helper_chown(ap->a_vp, vap->va_uid, vap->va_gid,
					 ap->a_cred,
					 &cur_uid, &cur_gid, &cur_mode);
		if (error == 0) {
			hammer_guid_to_uuid(&uuid_uid, cur_uid);
			hammer_guid_to_uuid(&uuid_gid, cur_gid);
			if (bcmp(&uuid_uid, &ip->ino_data.uid,
				 sizeof(uuid_uid)) ||
			    bcmp(&uuid_gid, &ip->ino_data.gid,
				 sizeof(uuid_gid)) ||
			    ip->ino_data.mode != cur_mode
			) {
				ip->ino_data.uid = uuid_uid;
				ip->ino_data.gid = uuid_gid;
				ip->ino_data.mode = cur_mode;
				ip->ino_data.ctime = trans.time;
				modflags |= HAMMER_INODE_DDIRTY;
			}
			kflags |= NOTE_ATTRIB;
		}
	}
	while (vap->va_size != VNOVAL && ip->ino_data.size != vap->va_size) {
		switch(ap->a_vp->v_type) {
		case VREG:
			if (vap->va_size == ip->ino_data.size)
				break;

			/*
			 * Log the operation if in fast-fsync mode or if
			 * there are unterminated redo write records present.
			 *
			 * The second check is needed so the recovery code
			 * properly truncates write redos even if nominal
			 * REDO operations is turned off due to excessive
			 * writes, because the related records might be
			 * destroyed and never lay down a TERM_WRITE.
			 */
			if ((ip->flags & HAMMER_INODE_REDO) ||
			    (ip->flags & HAMMER_INODE_RDIRTY)) {
				error = hammer_generate_redo(&trans, ip,
							     vap->va_size,
							     HAMMER_REDO_TRUNC,
							     NULL, 0);
			}
			blksize = hammer_blocksize(vap->va_size);

			/*
			 * XXX break atomicy, we can deadlock the backend
			 * if we do not release the lock.  Probably not a
			 * big deal here.
			 */
			if (vap->va_size < ip->ino_data.size) {
				nvtruncbuf(ap->a_vp, vap->va_size,
					   blksize,
					   hammer_blockoff(vap->va_size));
				truncating = 1;
				kflags |= NOTE_WRITE;
			} else {
				nvextendbuf(ap->a_vp,
					    ip->ino_data.size,
					    vap->va_size,
					    hammer_blocksize(ip->ino_data.size),
					    hammer_blocksize(vap->va_size),
					    hammer_blockoff(ip->ino_data.size),
					    hammer_blockoff(vap->va_size),
					    0);
				truncating = 0;
				kflags |= NOTE_WRITE | NOTE_EXTEND;
			}
			ip->ino_data.size = vap->va_size;
			ip->ino_data.mtime = trans.time;
			/* XXX safe to use SDIRTY instead of DDIRTY here? */
			modflags |= HAMMER_INODE_MTIME | HAMMER_INODE_DDIRTY;

			/*
			 * On-media truncation is cached in the inode until
			 * the inode is synchronized.  We must immediately
			 * handle any frontend records.
			 */
			if (truncating) {
				hammer_ip_frontend_trunc(ip, vap->va_size);
#ifdef DEBUG_TRUNCATE
				if (HammerTruncIp == NULL)
					HammerTruncIp = ip;
#endif
				if ((ip->flags & HAMMER_INODE_TRUNCATED) == 0) {
					ip->flags |= HAMMER_INODE_TRUNCATED;
					ip->trunc_off = vap->va_size;
#ifdef DEBUG_TRUNCATE
					if (ip == HammerTruncIp)
					kprintf("truncate1 %016llx\n",
						(long long)ip->trunc_off);
#endif
				} else if (ip->trunc_off > vap->va_size) {
					ip->trunc_off = vap->va_size;
#ifdef DEBUG_TRUNCATE
					if (ip == HammerTruncIp)
					kprintf("truncate2 %016llx\n",
						(long long)ip->trunc_off);
#endif
				} else {
#ifdef DEBUG_TRUNCATE
					if (ip == HammerTruncIp)
					kprintf("truncate3 %016llx (ignored)\n",
						(long long)vap->va_size);
#endif
				}
			}

#if 0
			/*
			 * When truncating, nvtruncbuf() may have cleaned out
			 * a portion of the last block on-disk in the buffer
			 * cache.  We must clean out any frontend records
			 * for blocks beyond the new last block.
			 */
			aligned_size = (vap->va_size + (blksize - 1)) &
				       ~(int64_t)(blksize - 1);
			if (truncating && vap->va_size < aligned_size) {
				aligned_size -= blksize;
				hammer_ip_frontend_trunc(ip, aligned_size);
			}
#endif
			break;
		case VDATABASE:
			if ((ip->flags & HAMMER_INODE_TRUNCATED) == 0) {
				ip->flags |= HAMMER_INODE_TRUNCATED;
				ip->trunc_off = vap->va_size;
			} else if (ip->trunc_off > vap->va_size) {
				ip->trunc_off = vap->va_size;
			}
			hammer_ip_frontend_trunc(ip, vap->va_size);
			ip->ino_data.size = vap->va_size;
			ip->ino_data.mtime = trans.time;
			modflags |= HAMMER_INODE_MTIME | HAMMER_INODE_DDIRTY;
			kflags |= NOTE_ATTRIB;
			break;
		default:
			error = EINVAL;
			goto done;
		}
		break;
	}
	if (vap->va_atime.tv_sec != VNOVAL) {
		ip->ino_data.atime = hammer_timespec_to_time(&vap->va_atime);
		modflags |= HAMMER_INODE_ATIME;
		kflags |= NOTE_ATTRIB;
	}
	if (vap->va_mtime.tv_sec != VNOVAL) {
		ip->ino_data.mtime = hammer_timespec_to_time(&vap->va_mtime);
		modflags |= HAMMER_INODE_MTIME;
		kflags |= NOTE_ATTRIB;
	}
	if (vap->va_mode != (mode_t)VNOVAL) {
		mode_t   cur_mode = ip->ino_data.mode;
		uid_t cur_uid = hammer_to_unix_xid(&ip->ino_data.uid);
		gid_t cur_gid = hammer_to_unix_xid(&ip->ino_data.gid);

		error = vop_helper_chmod(ap->a_vp, vap->va_mode, ap->a_cred,
					 cur_uid, cur_gid, &cur_mode);
		if (error == 0 && ip->ino_data.mode != cur_mode) {
			ip->ino_data.mode = cur_mode;
			ip->ino_data.ctime = trans.time;
			modflags |= HAMMER_INODE_DDIRTY;
			kflags |= NOTE_ATTRIB;
		}
	}
done:
	if (error == 0)
		hammer_modify_inode(&trans, ip, modflags);
	hammer_done_transaction(&trans);
	hammer_knote(ap->a_vp, kflags);
	return (error);
}

/*
 * hammer_vop_nsymlink { nch, dvp, vpp, cred, vap, target }
 */
static
int
hammer_vop_nsymlink(struct vop_nsymlink_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	struct hammer_inode *nip;
	struct nchandle *nch;
	hammer_record_t record;
	int error;
	int bytes;

	ap->a_vap->va_type = VLNK;

	nch = ap->a_nch;
	dip = VTOI(ap->a_dvp);

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);
	if ((error = hammer_checkspace(dip->hmp, HAMMER_CHKSPC_CREATE)) != 0)
		return (error);

	/*
	 * Create a transaction to cover the operations we perform.
	 */
	hammer_start_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsw;

	/*
	 * Create a new filesystem object of the requested type.  The
	 * returned inode will be referenced but not locked.
	 */

	error = hammer_create_inode(&trans, ap->a_vap, ap->a_cred,
				    dip, nch->ncp->nc_name, nch->ncp->nc_nlen,
				    NULL, &nip);
	if (error) {
		hammer_done_transaction(&trans);
		*ap->a_vpp = NULL;
		return (error);
	}

	/*
	 * Add a record representing the symlink.  symlink stores the link
	 * as pure data, not a string, and is no \0 terminated.
	 */
	if (error == 0) {
		bytes = strlen(ap->a_target);

		if (bytes <= HAMMER_INODE_BASESYMLEN) {
			bcopy(ap->a_target, nip->ino_data.ext.symlink, bytes);
		} else {
			record = hammer_alloc_mem_record(nip, bytes);
			record->type = HAMMER_MEM_RECORD_GENERAL;

			record->leaf.base.localization = nip->obj_localization +
							 HAMMER_LOCALIZE_MISC;
			record->leaf.base.key = HAMMER_FIXKEY_SYMLINK;
			record->leaf.base.rec_type = HAMMER_RECTYPE_FIX;
			record->leaf.data_len = bytes;
			KKASSERT(HAMMER_SYMLINK_NAME_OFF == 0);
			bcopy(ap->a_target, record->data->symlink.name, bytes);
			error = hammer_ip_add_record(&trans, record);
		}

		/*
		 * Set the file size to the length of the link.
		 */
		if (error == 0) {
			nip->ino_data.size = bytes;
			hammer_modify_inode(&trans, nip, HAMMER_INODE_DDIRTY);
		}
	}
	if (error == 0)
		error = hammer_ip_add_directory(&trans, dip, nch->ncp->nc_name,
						nch->ncp->nc_nlen, nip);

	/*
	 * Finish up.
	 */
	if (error) {
		hammer_rel_inode(nip, 0);
		*ap->a_vpp = NULL;
	} else {
		error = hammer_get_vnode(nip, ap->a_vpp);
		hammer_rel_inode(nip, 0);
		if (error == 0) {
			cache_setunresolved(ap->a_nch);
			cache_setvp(ap->a_nch, *ap->a_vpp);
			hammer_knote(ap->a_dvp, NOTE_WRITE);
		}
	}
	hammer_done_transaction(&trans);
	return (error);
}

/*
 * hammer_vop_nwhiteout { nch, dvp, cred, flags }
 */
static
int
hammer_vop_nwhiteout(struct vop_nwhiteout_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *dip;
	int error;

	dip = VTOI(ap->a_dvp);

	if (hammer_nohistory(dip) == 0 &&
	    (error = hammer_checkspace(dip->hmp, HAMMER_CHKSPC_CREATE)) != 0) {
		return (error);
	}

	hammer_start_transaction(&trans, dip->hmp);
	++hammer_stats_file_iopsw;
	error = hammer_dounlink(&trans, ap->a_nch, ap->a_dvp,
				ap->a_cred, ap->a_flags, -1);
	hammer_done_transaction(&trans);

	return (error);
}

/*
 * hammer_vop_ioctl { vp, command, data, fflag, cred }
 */
static
int
hammer_vop_ioctl(struct vop_ioctl_args *ap)
{
	struct hammer_inode *ip = ap->a_vp->v_data;

	++hammer_stats_file_iopsr;
	return(hammer_ioctl(ip, ap->a_command, ap->a_data,
			    ap->a_fflag, ap->a_cred));
}

static
int
hammer_vop_mountctl(struct vop_mountctl_args *ap)
{
	static const struct mountctl_opt extraopt[] = {
		{ HMNT_NOHISTORY, 	"nohistory" },
		{ HMNT_MASTERID,	"master" },
		{ 0, NULL}

	};
	struct hammer_mount *hmp;
	struct mount *mp;
	int usedbytes;
	int error;

	error = 0;
	usedbytes = 0;
	mp = ap->a_head.a_ops->head.vv_mount;
	KKASSERT(mp->mnt_data != NULL);
	hmp = (struct hammer_mount *)mp->mnt_data;

	switch(ap->a_op) {

	case MOUNTCTL_SET_EXPORT:
		if (ap->a_ctllen != sizeof(struct export_args))
			error = EINVAL;
		else
			error = hammer_vfs_export(mp, ap->a_op,
				      (const struct export_args *)ap->a_ctl);
		break;
	case MOUNTCTL_MOUNTFLAGS:
	{
		/*
		 * Call standard mountctl VOP function
		 * so we get user mount flags.
		 */
		error = vop_stdmountctl(ap);
		if (error)
			break;

		usedbytes = *ap->a_res;

		if (usedbytes > 0 && usedbytes < ap->a_buflen) {
			usedbytes += vfs_flagstostr(hmp->hflags, extraopt, ap->a_buf,
						    ap->a_buflen - usedbytes,
						    &error);
		}

		*ap->a_res += usedbytes;
		break;
	}
	default:
		error = vop_stdmountctl(ap);
		break;
	}
	return(error);
}

/*
 * hammer_vop_strategy { vp, bio }
 *
 * Strategy call, used for regular file read & write only.  Note that the
 * bp may represent a cluster.
 *
 * To simplify operation and allow better optimizations in the future,
 * this code does not make any assumptions with regards to buffer alignment
 * or size.
 */
static
int
hammer_vop_strategy(struct vop_strategy_args *ap)
{
	struct buf *bp;
	int error;

	bp = ap->a_bio->bio_buf;

	switch(bp->b_cmd) {
	case BUF_CMD_READ:
		error = hammer_vop_strategy_read(ap);
		break;
	case BUF_CMD_WRITE:
		error = hammer_vop_strategy_write(ap);
		break;
	default:
		bp->b_error = error = EINVAL;
		bp->b_flags |= B_ERROR;
		biodone(ap->a_bio);
		break;
	}
	return (error);
}

/*
 * Read from a regular file.  Iterate the related records and fill in the
 * BIO/BUF.  Gaps are zero-filled.
 *
 * The support code in hammer_object.c should be used to deal with mixed
 * in-memory and on-disk records.
 *
 * NOTE: Can be called from the cluster code with an oversized buf.
 *
 * XXX atime update
 */
static
int
hammer_vop_strategy_read(struct vop_strategy_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	struct hammer_inode *dip;
	struct hammer_cursor cursor;
	hammer_base_elm_t base;
	hammer_off_t disk_offset;
	struct bio *bio;
	struct bio *nbio;
	struct buf *bp;
	int64_t rec_offset;
	int64_t ran_end;
	int64_t tmp64;
	int error;
	int boff;
	int roff;
	int n;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = ap->a_vp->v_data;

	/*
	 * The zone-2 disk offset may have been set by the cluster code via
	 * a BMAP operation, or else should be NOOFFSET.
	 *
	 * Checking the high bits for a match against zone-2 should suffice.
	 */
	nbio = push_bio(bio);
	if ((nbio->bio_offset & HAMMER_OFF_ZONE_MASK) ==
	    HAMMER_ZONE_LARGE_DATA) {
		error = hammer_io_direct_read(ip->hmp, nbio, NULL);
		return (error);
	}

	/*
	 * Well, that sucked.  Do it the hard way.  If all the stars are
	 * aligned we may still be able to issue a direct-read.
	 */
	hammer_simple_transaction(&trans, ip->hmp);
	hammer_init_cursor(&trans, &cursor, &ip->cache[1], ip);

	/*
	 * Key range (begin and end inclusive) to scan.  Note that the key's
	 * stored in the actual records represent BASE+LEN, not BASE.  The
	 * first record containing bio_offset will have a key > bio_offset.
	 */
	cursor.key_beg.localization = ip->obj_localization +
				      HAMMER_LOCALIZE_MISC;
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	cursor.key_beg.key = bio->bio_offset + 1;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	cursor.key_end = cursor.key_beg;
	KKASSERT(ip->ino_data.obj_type == HAMMER_OBJTYPE_REGFILE);
#if 0
	if (ip->ino_data.obj_type == HAMMER_OBJTYPE_DBFILE) {
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DB;
		cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
	} else
#endif
	{
		ran_end = bio->bio_offset + bp->b_bufsize;
		cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
		cursor.key_end.rec_type = HAMMER_RECTYPE_DATA;
		tmp64 = ran_end + MAXPHYS + 1;	/* work-around GCC-4 bug */
		if (tmp64 < ran_end)
			cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
		else
			cursor.key_end.key = ran_end + MAXPHYS + 1;
	}
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(&cursor);
	boff = 0;

	while (error == 0) {
		/*
		 * Get the base file offset of the record.  The key for
		 * data records is (base + bytes) rather then (base).
		 */
		base = &cursor.leaf->base;
		rec_offset = base->key - cursor.leaf->data_len;

		/*
		 * Calculate the gap, if any, and zero-fill it.
		 *
		 * n is the offset of the start of the record verses our
		 * current seek offset in the bio.
		 */
		n = (int)(rec_offset - (bio->bio_offset + boff));
		if (n > 0) {
			if (n > bp->b_bufsize - boff)
				n = bp->b_bufsize - boff;
			bzero((char *)bp->b_data + boff, n);
			boff += n;
			n = 0;
		}

		/*
		 * Calculate the data offset in the record and the number
		 * of bytes we can copy.
		 *
		 * There are two degenerate cases.  First, boff may already
		 * be at bp->b_bufsize.  Secondly, the data offset within
		 * the record may exceed the record's size.
		 */
		roff = -n;
		rec_offset += roff;
		n = cursor.leaf->data_len - roff;
		if (n <= 0) {
			kprintf("strategy_read: bad n=%d roff=%d\n", n, roff);
			n = 0;
		} else if (n > bp->b_bufsize - boff) {
			n = bp->b_bufsize - boff;
		}

		/*
		 * Deal with cached truncations.  This cool bit of code
		 * allows truncate()/ftruncate() to avoid having to sync
		 * the file.
		 *
		 * If the frontend is truncated then all backend records are
		 * subject to the frontend's truncation.
		 *
		 * If the backend is truncated then backend records on-disk
		 * (but not in-memory) are subject to the backend's
		 * truncation.  In-memory records owned by the backend
		 * represent data written after the truncation point on the
		 * backend and must not be truncated.
		 *
		 * Truncate operations deal with frontend buffer cache
		 * buffers and frontend-owned in-memory records synchronously.
		 */
		if (ip->flags & HAMMER_INODE_TRUNCATED) {
			if (hammer_cursor_ondisk(&cursor)/* ||
			    cursor.iprec->flush_state == HAMMER_FST_FLUSH*/) {
				if (ip->trunc_off <= rec_offset)
					n = 0;
				else if (ip->trunc_off < rec_offset + n)
					n = (int)(ip->trunc_off - rec_offset);
			}
		}
		if (ip->sync_flags & HAMMER_INODE_TRUNCATED) {
			if (hammer_cursor_ondisk(&cursor)) {
				if (ip->sync_trunc_off <= rec_offset)
					n = 0;
				else if (ip->sync_trunc_off < rec_offset + n)
					n = (int)(ip->sync_trunc_off - rec_offset);
			}
		}

		/*
		 * Try to issue a direct read into our bio if possible,
		 * otherwise resolve the element data into a hammer_buffer
		 * and copy.
		 *
		 * The buffer on-disk should be zerod past any real
		 * truncation point, but may not be for any synthesized
		 * truncation point from above.
		 */
		disk_offset = cursor.leaf->data_offset + roff;
		if (boff == 0 && n == bp->b_bufsize &&
		    hammer_cursor_ondisk(&cursor) &&
		    (disk_offset & HAMMER_BUFMASK) == 0) {
			KKASSERT((disk_offset & HAMMER_OFF_ZONE_MASK) ==
				 HAMMER_ZONE_LARGE_DATA);
			nbio->bio_offset = disk_offset;
			error = hammer_io_direct_read(trans.hmp, nbio,
						      cursor.leaf);
			goto done;
		} else if (n) {
			error = hammer_ip_resolve_data(&cursor);
			if (error == 0) {
				bcopy((char *)cursor.data + roff,
				      (char *)bp->b_data + boff, n);
			}
		}
		if (error)
			break;

		/*
		 * Iterate until we have filled the request.
		 */
		boff += n;
		if (boff == bp->b_bufsize)
			break;
		error = hammer_ip_next(&cursor);
	}

	/*
	 * There may have been a gap after the last record
	 */
	if (error == ENOENT)
		error = 0;
	if (error == 0 && boff != bp->b_bufsize) {
		KKASSERT(boff < bp->b_bufsize);
		bzero((char *)bp->b_data + boff, bp->b_bufsize - boff);
		/* boff = bp->b_bufsize; */
	}
	bp->b_resid = 0;
	bp->b_error = error;
	if (error)
		bp->b_flags |= B_ERROR;
	biodone(ap->a_bio);

done:
	/*
	 * Cache the b-tree node for the last data read in cache[1].
	 *
	 * If we hit the file EOF then also cache the node in the
	 * governing director's cache[3], it will be used to initialize
	 * the inode's cache[1] for any inodes looked up via the directory.
	 *
	 * This doesn't reduce disk accesses since the B-Tree chain is
	 * likely cached, but it does reduce cpu overhead when looking
	 * up file offsets for cpdup/tar/cpio style iterations.
	 */
	if (cursor.node)
		hammer_cache_node(&ip->cache[1], cursor.node);
	if (ran_end >= ip->ino_data.size) {
		dip = hammer_find_inode(&trans, ip->ino_data.parent_obj_id,
					ip->obj_asof, ip->obj_localization);
		if (dip) {
			hammer_cache_node(&dip->cache[3], cursor.node);
			hammer_rel_inode(dip, 0);
		}
	}
	hammer_done_cursor(&cursor);
	hammer_done_transaction(&trans);
	return(error);
}

/*
 * BMAP operation - used to support cluster_read() only.
 *
 * (struct vnode *vp, off_t loffset, off_t *doffsetp, int *runp, int *runb)
 *
 * This routine may return EOPNOTSUPP if the opration is not supported for
 * the specified offset.  The contents of the pointer arguments do not
 * need to be initialized in that case. 
 *
 * If a disk address is available and properly aligned return 0 with 
 * *doffsetp set to the zone-2 address, and *runp / *runb set appropriately
 * to the run-length relative to that offset.  Callers may assume that
 * *doffsetp is valid if 0 is returned, even if *runp is not sufficiently
 * large, so return EOPNOTSUPP if it is not sufficiently large.
 */
static
int
hammer_vop_bmap(struct vop_bmap_args *ap)
{
	struct hammer_transaction trans;
	struct hammer_inode *ip;
	struct hammer_cursor cursor;
	hammer_base_elm_t base;
	int64_t rec_offset;
	int64_t ran_end;
	int64_t tmp64;
	int64_t base_offset;
	int64_t base_disk_offset;
	int64_t last_offset;
	hammer_off_t last_disk_offset;
	hammer_off_t disk_offset;
	int	rec_len;
	int	error;
	int	blksize;

	++hammer_stats_file_iopsr;
	ip = ap->a_vp->v_data;

	/*
	 * We can only BMAP regular files.  We can't BMAP database files,
	 * directories, etc.
	 */
	if (ip->ino_data.obj_type != HAMMER_OBJTYPE_REGFILE)
		return(EOPNOTSUPP);

	/*
	 * bmap is typically called with runp/runb both NULL when used
	 * for writing.  We do not support BMAP for writing atm.
	 */
	if (ap->a_cmd != BUF_CMD_READ)
		return(EOPNOTSUPP);

	/*
	 * Scan the B-Tree to acquire blockmap addresses, then translate
	 * to raw addresses.
	 */
	hammer_simple_transaction(&trans, ip->hmp);
#if 0
	kprintf("bmap_beg %016llx ip->cache %p\n",
		(long long)ap->a_loffset, ip->cache[1]);
#endif
	hammer_init_cursor(&trans, &cursor, &ip->cache[1], ip);

	/*
	 * Key range (begin and end inclusive) to scan.  Note that the key's
	 * stored in the actual records represent BASE+LEN, not BASE.  The
	 * first record containing bio_offset will have a key > bio_offset.
	 */
	cursor.key_beg.localization = ip->obj_localization +
				      HAMMER_LOCALIZE_MISC;
	cursor.key_beg.obj_id = ip->obj_id;
	cursor.key_beg.create_tid = 0;
	cursor.key_beg.delete_tid = 0;
	cursor.key_beg.obj_type = 0;
	if (ap->a_runb)
		cursor.key_beg.key = ap->a_loffset - MAXPHYS + 1;
	else
		cursor.key_beg.key = ap->a_loffset + 1;
	if (cursor.key_beg.key < 0)
		cursor.key_beg.key = 0;
	cursor.asof = ip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_ASOF;

	cursor.key_end = cursor.key_beg;
	KKASSERT(ip->ino_data.obj_type == HAMMER_OBJTYPE_REGFILE);

	ran_end = ap->a_loffset + MAXPHYS;
	cursor.key_beg.rec_type = HAMMER_RECTYPE_DATA;
	cursor.key_end.rec_type = HAMMER_RECTYPE_DATA;
	tmp64 = ran_end + MAXPHYS + 1;	/* work-around GCC-4 bug */
	if (tmp64 < ran_end)
		cursor.key_end.key = 0x7FFFFFFFFFFFFFFFLL;
	else
		cursor.key_end.key = ran_end + MAXPHYS + 1;

	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE;

	error = hammer_ip_first(&cursor);
	base_offset = last_offset = 0;
	base_disk_offset = last_disk_offset = 0;

	while (error == 0) {
		/*
		 * Get the base file offset of the record.  The key for
		 * data records is (base + bytes) rather then (base).
		 *
		 * NOTE: rec_offset + rec_len may exceed the end-of-file.
		 * The extra bytes should be zero on-disk and the BMAP op
		 * should still be ok.
		 */
		base = &cursor.leaf->base;
		rec_offset = base->key - cursor.leaf->data_len;
		rec_len    = cursor.leaf->data_len;

		/*
		 * Incorporate any cached truncation.
		 *
		 * NOTE: Modifications to rec_len based on synthesized
		 * truncation points remove the guarantee that any extended
		 * data on disk is zero (since the truncations may not have
		 * taken place on-media yet).
		 */
		if (ip->flags & HAMMER_INODE_TRUNCATED) {
			if (hammer_cursor_ondisk(&cursor) ||
			    cursor.iprec->flush_state == HAMMER_FST_FLUSH) {
				if (ip->trunc_off <= rec_offset)
					rec_len = 0;
				else if (ip->trunc_off < rec_offset + rec_len)
					rec_len = (int)(ip->trunc_off - rec_offset);
			}
		}
		if (ip->sync_flags & HAMMER_INODE_TRUNCATED) {
			if (hammer_cursor_ondisk(&cursor)) {
				if (ip->sync_trunc_off <= rec_offset)
					rec_len = 0;
				else if (ip->sync_trunc_off < rec_offset + rec_len)
					rec_len = (int)(ip->sync_trunc_off - rec_offset);
			}
		}

		/*
		 * Accumulate information.  If we have hit a discontiguous
		 * block reset base_offset unless we are already beyond the
		 * requested offset.  If we are, that's it, we stop.
		 */
		if (error)
			break;
		if (hammer_cursor_ondisk(&cursor)) {
			disk_offset = cursor.leaf->data_offset;
			if (rec_offset != last_offset ||
			    disk_offset != last_disk_offset) {
				if (rec_offset > ap->a_loffset)
					break;
				base_offset = rec_offset;
				base_disk_offset = disk_offset;
			}
			last_offset = rec_offset + rec_len;
			last_disk_offset = disk_offset + rec_len;
		}
		error = hammer_ip_next(&cursor);
	}

#if 0
	kprintf("BMAP %016llx:  %016llx - %016llx\n",
		(long long)ap->a_loffset,
		(long long)base_offset,
		(long long)last_offset);
	kprintf("BMAP %16s:  %016llx - %016llx\n", "",
		(long long)base_disk_offset,
		(long long)last_disk_offset);
#endif

	if (cursor.node) {
		hammer_cache_node(&ip->cache[1], cursor.node);
#if 0
		kprintf("bmap_end2 %016llx ip->cache %p\n",
			(long long)ap->a_loffset, ip->cache[1]);
#endif
	}
	hammer_done_cursor(&cursor);
	hammer_done_transaction(&trans);

	/*
	 * If we couldn't find any records or the records we did find were
	 * all behind the requested offset, return failure.  A forward
	 * truncation can leave a hole w/ no on-disk records.
	 */
	if (last_offset == 0 || last_offset < ap->a_loffset)
		return (EOPNOTSUPP);

	/*
	 * Figure out the block size at the requested offset and adjust
	 * our limits so the cluster_read() does not create inappropriately
	 * sized buffer cache buffers.
	 */
	blksize = hammer_blocksize(ap->a_loffset);
	if (hammer_blocksize(base_offset) != blksize) {
		base_offset = hammer_blockdemarc(base_offset, ap->a_loffset);
	}
	if (last_offset != ap->a_loffset &&
	    hammer_blocksize(last_offset - 1) != blksize) {
		last_offset = hammer_blockdemarc(ap->a_loffset,
						 last_offset - 1);
	}

	/*
	 * Returning EOPNOTSUPP simply prevents the direct-IO optimization
	 * from occuring.
	 */
	disk_offset = base_disk_offset + (ap->a_loffset - base_offset);

	if ((disk_offset & HAMMER_OFF_ZONE_MASK) != HAMMER_ZONE_LARGE_DATA) {
		/*
		 * Only large-data zones can be direct-IOd
		 */
		error = EOPNOTSUPP;
	} else if ((disk_offset & HAMMER_BUFMASK) ||
		   (last_offset - ap->a_loffset) < blksize) {
		/*
		 * doffsetp is not aligned or the forward run size does
		 * not cover a whole buffer, disallow the direct I/O.
		 */
		error = EOPNOTSUPP;
	} else {
		/*
		 * We're good.
		 */
		*ap->a_doffsetp = disk_offset;
		if (ap->a_runb) {
			*ap->a_runb = ap->a_loffset - base_offset;
			KKASSERT(*ap->a_runb >= 0);
		}
		if (ap->a_runp) {
			*ap->a_runp = last_offset - ap->a_loffset;
			KKASSERT(*ap->a_runp >= 0);
		}
		error = 0;
	}
	return(error);
}

/*
 * Write to a regular file.   Because this is a strategy call the OS is
 * trying to actually get data onto the media.
 */
static
int
hammer_vop_strategy_write(struct vop_strategy_args *ap)
{
	hammer_record_t record;
	hammer_mount_t hmp;
	hammer_inode_t ip;
	struct bio *bio;
	struct buf *bp;
	int blksize;
	int bytes;
	int error;

	bio = ap->a_bio;
	bp = bio->bio_buf;
	ip = ap->a_vp->v_data;
	hmp = ip->hmp;

	blksize = hammer_blocksize(bio->bio_offset);
	KKASSERT(bp->b_bufsize == blksize);

	if (ip->flags & HAMMER_INODE_RO) {
		bp->b_error = EROFS;
		bp->b_flags |= B_ERROR;
		biodone(ap->a_bio);
		return(EROFS);
	}

	/*
	 * Interlock with inode destruction (no in-kernel or directory
	 * topology visibility).  If we queue new IO while trying to
	 * destroy the inode we can deadlock the vtrunc call in
	 * hammer_inode_unloadable_check().
	 *
	 * Besides, there's no point flushing a bp associated with an
	 * inode that is being destroyed on-media and has no kernel
	 * references.
	 */
	if ((ip->flags | ip->sync_flags) &
	    (HAMMER_INODE_DELETING|HAMMER_INODE_DELETED)) {
		bp->b_resid = 0;
		biodone(ap->a_bio);
		return(0);
	}

	/*
	 * Reserve space and issue a direct-write from the front-end. 
	 * NOTE: The direct_io code will hammer_bread/bcopy smaller
	 * allocations.
	 *
	 * An in-memory record will be installed to reference the storage
	 * until the flusher can get to it.
	 *
	 * Since we own the high level bio the front-end will not try to
	 * do a direct-read until the write completes.
	 *
	 * NOTE: The only time we do not reserve a full-sized buffers
	 * worth of data is if the file is small.  We do not try to
	 * allocate a fragment (from the small-data zone) at the end of
	 * an otherwise large file as this can lead to wildly separated
	 * data.
	 */
	KKASSERT((bio->bio_offset & HAMMER_BUFMASK) == 0);
	KKASSERT(bio->bio_offset < ip->ino_data.size);
	if (bio->bio_offset || ip->ino_data.size > HAMMER_BUFSIZE / 2)
		bytes = bp->b_bufsize;
	else
		bytes = ((int)ip->ino_data.size + 15) & ~15;

	record = hammer_ip_add_bulk(ip, bio->bio_offset, bp->b_data,
				    bytes, &error);

	/*
	 * B_VFSFLAG1 indicates that a REDO_WRITE entry was generated
	 * in hammer_vop_write().  We must flag the record so the proper
	 * REDO_TERM_WRITE entry is generated during the flush.
	 */
	if (record) {
		if (bp->b_flags & B_VFSFLAG1) {
			record->flags |= HAMMER_RECF_REDO;
			bp->b_flags &= ~B_VFSFLAG1;
		}
		hammer_io_direct_write(hmp, bio, record);
		if (ip->rsv_recs > 1 && hmp->rsv_recs > hammer_limit_recs)
			hammer_flush_inode(ip, 0);
	} else {
		bp->b_bio2.bio_offset = NOOFFSET;
		bp->b_error = error;
		bp->b_flags |= B_ERROR;
		biodone(ap->a_bio);
	}
	return(error);
}

/*
 * dounlink - disconnect a directory entry
 *
 * XXX whiteout support not really in yet
 */
static int
hammer_dounlink(hammer_transaction_t trans, struct nchandle *nch,
		struct vnode *dvp, struct ucred *cred, 
		int flags, int isdir)
{
	struct namecache *ncp;
	hammer_inode_t dip;
	hammer_inode_t ip;
	struct hammer_cursor cursor;
	int64_t namekey;
	u_int32_t max_iterations;
	int nlen, error;

	/*
	 * Calculate the namekey and setup the key range for the scan.  This
	 * works kinda like a chained hash table where the lower 32 bits
	 * of the namekey synthesize the chain.
	 *
	 * The key range is inclusive of both key_beg and key_end.
	 */
	dip = VTOI(dvp);
	ncp = nch->ncp;

	if (dip->flags & HAMMER_INODE_RO)
		return (EROFS);

	namekey = hammer_directory_namekey(dip, ncp->nc_name, ncp->nc_nlen,
					   &max_iterations);
retry:
	hammer_init_cursor(trans, &cursor, &dip->cache[1], dip);
	cursor.key_beg.localization = dip->obj_localization +
				      hammer_dir_localization(dip);
        cursor.key_beg.obj_id = dip->obj_id;
	cursor.key_beg.key = namekey;
        cursor.key_beg.create_tid = 0;
        cursor.key_beg.delete_tid = 0;
        cursor.key_beg.rec_type = HAMMER_RECTYPE_DIRENTRY;
        cursor.key_beg.obj_type = 0;

	cursor.key_end = cursor.key_beg;
	cursor.key_end.key += max_iterations;
	cursor.asof = dip->obj_asof;
	cursor.flags |= HAMMER_CURSOR_END_INCLUSIVE | HAMMER_CURSOR_ASOF;

	/*
	 * Scan all matching records (the chain), locate the one matching
	 * the requested path component.  info->last_error contains the
	 * error code on search termination and could be 0, ENOENT, or
	 * something else.
	 *
	 * The hammer_ip_*() functions merge in-memory records with on-disk
	 * records for the purposes of the search.
	 */
	error = hammer_ip_first(&cursor);

	while (error == 0) {
		error = hammer_ip_resolve_data(&cursor);
		if (error)
			break;
		nlen = cursor.leaf->data_len - HAMMER_ENTRY_NAME_OFF;
		KKASSERT(nlen > 0);
		if (ncp->nc_nlen == nlen &&
		    bcmp(ncp->nc_name, cursor.data->entry.name, nlen) == 0) {
			break;
		}
		error = hammer_ip_next(&cursor);
	}

	/*
	 * If all is ok we have to get the inode so we can adjust nlinks.
	 * To avoid a deadlock with the flusher we must release the inode
	 * lock on the directory when acquiring the inode for the entry.
	 *
	 * If the target is a directory, it must be empty.
	 */
	if (error == 0) {
		hammer_unlock(&cursor.ip->lock);
		ip = hammer_get_inode(trans, dip, cursor.data->entry.obj_id,
				      dip->hmp->asof,
				      cursor.data->entry.localization,
				      0, &error);
		hammer_lock_sh(&cursor.ip->lock);
		if (error == ENOENT) {
			kprintf("HAMMER: WARNING: Removing "
				"dirent w/missing inode \"%s\"\n"
				"\tobj_id = %016llx\n",
				ncp->nc_name,
				(long long)cursor.data->entry.obj_id);
			error = 0;
		}

		/*
		 * If isdir >= 0 we validate that the entry is or is not a
		 * directory.  If isdir < 0 we don't care.
		 */
		if (error == 0 && isdir >= 0 && ip) {
			if (isdir &&
			    ip->ino_data.obj_type != HAMMER_OBJTYPE_DIRECTORY) {
				error = ENOTDIR;
			} else if (isdir == 0 &&
			    ip->ino_data.obj_type == HAMMER_OBJTYPE_DIRECTORY) {
				error = EISDIR;
			}
		}

		/*
		 * If we are trying to remove a directory the directory must
		 * be empty.
		 *
		 * The check directory code can loop and deadlock/retry.  Our
		 * own cursor's node locks must be released to avoid a 3-way
		 * deadlock with the flusher if the check directory code
		 * blocks.
		 *
		 * If any changes whatsoever have been made to the cursor
		 * set EDEADLK and retry.
		 *
		 * WARNING: See warnings in hammer_unlock_cursor()
		 *	    function.
		 */
		if (error == 0 && ip && ip->ino_data.obj_type ==
				        HAMMER_OBJTYPE_DIRECTORY) {
			hammer_unlock_cursor(&cursor);
			error = hammer_ip_check_directory_empty(trans, ip);
			hammer_lock_cursor(&cursor);
			if (cursor.flags & HAMMER_CURSOR_RETEST) {
				kprintf("HAMMER: Warning: avoided deadlock "
					"on rmdir '%s'\n",
					ncp->nc_name);
				error = EDEADLK;
			}
		}

		/*
		 * Delete the directory entry.
		 *
		 * WARNING: hammer_ip_del_directory() may have to terminate
		 * the cursor to avoid a deadlock.  It is ok to call
		 * hammer_done_cursor() twice.
		 */
		if (error == 0) {
			error = hammer_ip_del_directory(trans, &cursor,
							dip, ip);
		}
		hammer_done_cursor(&cursor);
		if (error == 0) {
			cache_setunresolved(nch);
			cache_setvp(nch, NULL);

			/*
			 * XXX locking.  Note: ip->vp might get ripped out
			 * when we setunresolved() the nch since we had
			 * no other reference to it.  In that case ip->vp
			 * will be NULL.
			 */
			if (ip && ip->vp) {
				hammer_knote(ip->vp, NOTE_DELETE);
				cache_inval_vp(ip->vp, CINV_DESTROY);
			}
		}
		if (ip)
			hammer_rel_inode(ip, 0);
	} else {
		hammer_done_cursor(&cursor);
	}
	if (error == EDEADLK)
		goto retry;

	return (error);
}

/************************************************************************
 *			    FIFO AND SPECFS OPS				*
 ************************************************************************
 *
 */

static int
hammer_vop_fifoclose (struct vop_close_args *ap)
{
	/* XXX update itimes */
	return (VOCALL(&fifo_vnode_vops, &ap->a_head));
}

static int
hammer_vop_fiforead (struct vop_read_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	/* XXX update access time */
	return (error);
}

static int
hammer_vop_fifowrite (struct vop_write_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	/* XXX update access time */
	return (error);
}

static
int
hammer_vop_fifokqfilter(struct vop_kqfilter_args *ap)
{
	int error;

	error = VOCALL(&fifo_vnode_vops, &ap->a_head);
	if (error)
		error = hammer_vop_kqfilter(ap);
	return(error);
}

/************************************************************************
 *			    KQFILTER OPS				*
 ************************************************************************
 *
 */
static void filt_hammerdetach(struct knote *kn);
static int filt_hammerread(struct knote *kn, long hint);
static int filt_hammerwrite(struct knote *kn, long hint);
static int filt_hammervnode(struct knote *kn, long hint);

static struct filterops hammerread_filtops =
	{ FILTEROP_ISFD, NULL, filt_hammerdetach, filt_hammerread };
static struct filterops hammerwrite_filtops =
	{ FILTEROP_ISFD, NULL, filt_hammerdetach, filt_hammerwrite };
static struct filterops hammervnode_filtops =
	{ FILTEROP_ISFD, NULL, filt_hammerdetach, filt_hammervnode };

static
int
hammer_vop_kqfilter(struct vop_kqfilter_args *ap)
{
	struct vnode *vp = ap->a_vp;
	struct knote *kn = ap->a_kn;

	switch (kn->kn_filter) {
	case EVFILT_READ:
		kn->kn_fop = &hammerread_filtops;
		break;
	case EVFILT_WRITE:
		kn->kn_fop = &hammerwrite_filtops;
		break;
	case EVFILT_VNODE:
		kn->kn_fop = &hammervnode_filtops;
		break;
	default:
		return (EOPNOTSUPP);
	}

	kn->kn_hook = (caddr_t)vp;

	/* XXX: kq token actually protects the list */
	lwkt_gettoken(&vp->v_token);
	knote_insert(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
	lwkt_reltoken(&vp->v_token);

	return(0);
}

static void
filt_hammerdetach(struct knote *kn)
{
	struct vnode *vp = (void *)kn->kn_hook;

	lwkt_gettoken(&vp->v_token);
	knote_remove(&vp->v_pollinfo.vpi_kqinfo.ki_note, kn);
	lwkt_reltoken(&vp->v_token);
}

static int
filt_hammerread(struct knote *kn, long hint)
{
	struct vnode *vp = (void *)kn->kn_hook;
	hammer_inode_t ip = VTOI(vp);

	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
		return(1);
	}
	kn->kn_data = ip->ino_data.size - kn->kn_fp->f_offset;
	return (kn->kn_data != 0);
}

static int
filt_hammerwrite(struct knote *kn, long hint)
{
	if (hint == NOTE_REVOKE)
		kn->kn_flags |= (EV_EOF | EV_ONESHOT);
	kn->kn_data = 0;
	return (1);
}

static int
filt_hammervnode(struct knote *kn, long hint)
{
	if (kn->kn_sfflags & hint)
		kn->kn_fflags |= hint;
	if (hint == NOTE_REVOKE) {
		kn->kn_flags |= EV_EOF;
		return (1);
	}
	return (kn->kn_fflags != 0);
}

