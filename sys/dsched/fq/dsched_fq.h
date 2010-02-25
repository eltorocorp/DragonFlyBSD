/*
 * Copyright (c) 2009, 2010 The DragonFly Project.  All rights reserved.
 *
 * This code is derived from software contributed to The DragonFly Project
 * by Alex Hornung <ahornung@gmail.com>
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
 */
#ifndef	_DSCHED_FQ_H_
#define	_DSCHED_FQ_H_

#if defined(_KERNEL) || defined(_KERNEL_STRUCTURES)

#ifndef _SYS_QUEUE_H_
#include <sys/queue.h>
#endif
#ifndef _SYS_BIO_H_
#include <sys/bio.h>
#endif
#ifndef _SYS_BIOTRACK_H_
#include <sys/biotrack.h>
#endif
#ifndef _SYS_SPINLOCK_H_
#include <sys/spinlock.h>
#endif
/*
#define	FQ_IOQ_INIT(x)		lockinit(&(x)->fq_lock, "fqioq", 0, LK_CANRECURSE)
#define FQ_IOQ_LOCK(x)		lockmgr(&(x)->fq_lock, LK_EXCLUSIVE)
#define	FQ_IOQ_UNLOCK(x)	lockmgr(&(x)->fq_lock, LK_RELEASE)
*/

#define	FQ_FQMP_LOCKINIT(x)	spin_init(&(x)->lock)
#define	FQ_FQP_LOCKINIT(x)	spin_init(&(x)->lock)
#define	FQ_DPRIV_LOCKINIT(x)	spin_init(&(x)->lock)
#define	FQ_GLOBAL_FQMP_LOCKINIT(x)	spin_init(&fq_fqmp_lock)


#define	FQ_FQMP_LOCK(x)		fq_reference_mpriv((x)); \
				spin_lock_wr(&(x)->lock)

#define	FQ_FQP_LOCK(x)		fq_reference_priv((x)); \
				spin_lock_wr(&(x)->lock)

#define	FQ_DPRIV_LOCK(x)	fq_reference_dpriv((x)); \
				spin_lock_wr(&(x)->lock)

#define	FQ_GLOBAL_FQMP_LOCK(x)	spin_lock_wr(&fq_fqmp_lock)


#define	FQ_FQMP_UNLOCK(x)	spin_unlock_wr(&(x)->lock); \
				fq_dereference_mpriv((x))

#define	FQ_FQP_UNLOCK(x)	spin_unlock_wr(&(x)->lock); \
				fq_dereference_priv((x))

#define	FQ_DPRIV_UNLOCK(x)	spin_unlock_wr(&(x)->lock); \
				fq_dereference_dpriv((x))

#define	FQ_GLOBAL_FQMP_UNLOCK(x) spin_unlock_wr(&fq_fqmp_lock)

#define	FQ_REBALANCE_TIMEOUT	1	/* in seconds */

/*
#define	FQ_INNERQ_INIT(x)	spin_init(&(x)->lock)
#define	FQ_INNERQ_UNINIT(x)	spin_uninit(&(x)->lock)
#define FQ_INNERQ_LOCK(x)	spin_lock_wr(&(x)->lock)
#define	FQ_INNERQ_UNLOCK(x)	spin_unlock_wr(&(x)->lock)
#define FQ_INNERQ_SLEEP(x,y,z)	ssleep((x), &(x)->lock, 0, y, z)
*/


struct disk;
struct proc;

#define	FQP_LINKED_DPRIV	0x01
#define	FQP_LINKED_FQMP		0x02

struct dsched_fq_priv {
	TAILQ_ENTRY(dsched_fq_priv)	link;
	TAILQ_ENTRY(dsched_fq_priv)	dlink;
	TAILQ_HEAD(, bio)	queue;

	struct	spinlock	lock;
	struct	disk		*dp;
	struct dsched_fq_dpriv	*dpriv;
	struct dsched_fq_mpriv	*fqmp;

	int32_t	qlength;
	int32_t	flags;

	int	refcount;
	int32_t	transactions;
	int32_t	avg_latency;
	int32_t	max_tp;
};

struct dsched_fq_dpriv {
	struct thread	*td;
	struct disk	*dp;
	struct spinlock	lock;
	int	refcount;

	int	avg_rq_time;	/* XXX: unused */
	int32_t	incomplete_tp;

	/* list contains all fq_priv for this disk */
	TAILQ_HEAD(, dsched_fq_priv)	fq_priv_list;
	TAILQ_ENTRY(dsched_fq_dpriv)	link;
};

struct dsched_fq_mpriv {
	struct spinlock	lock;
	int	refcount;
	TAILQ_HEAD(, dsched_fq_priv)	fq_priv_list;
	TAILQ_ENTRY(dsched_fq_mpriv)	link;
};




/* Bucket Magic */
struct dsched_fq_bucket {
	struct lock	bucket_lock;
	struct disk	*dp;

	int	nprocs;
	int	flags;
	int	prio;

	TAILQ_HEAD(, dsched_fq_mpriv)	fq_mpriv_list;

};


#define	FQ_BUCKET_ACTIVE	0x01



struct dsched_fq_priv	*fq_alloc_priv(struct disk *dp);
struct dsched_fq_dpriv	*fq_alloc_dpriv(struct disk *dp);
struct dsched_fq_mpriv	*fq_alloc_mpriv(void);
void	fq_balance_thread(struct dsched_fq_dpriv *dpriv);
void	fq_dispatcher(struct dsched_fq_dpriv *dpriv);
biodone_t		fq_completed;

void	fq_reference_dpriv(struct dsched_fq_dpriv *dpriv);
void	fq_reference_priv(struct dsched_fq_priv *fqp);
void	fq_reference_mpriv(struct dsched_fq_mpriv *fqmp);
void	fq_dereference_dpriv(struct dsched_fq_dpriv *dpriv);
void	fq_dereference_priv(struct dsched_fq_priv *fqp);
void	fq_dereference_mpriv(struct dsched_fq_mpriv *fqmp);

#endif /* _KERNEL || _KERNEL_STRUCTURES */


struct dsched_fq_stats {
	int32_t	fqmp_allocations;
	int32_t	fqp_allocations;
	int32_t	dpriv_allocations;

	int32_t	procs_limited;

	int32_t	transactions;
	int32_t	transactions_completed;
	int32_t	cancelled;

	int32_t	no_fqmp;
};

#endif /* _DSCHED_FQ_H_ */
