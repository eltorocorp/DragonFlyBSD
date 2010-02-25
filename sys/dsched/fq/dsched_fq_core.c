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
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/diskslice.h>
#include <sys/disk.h>
#include <machine/atomic.h>
#include <sys/malloc.h>
#include <sys/thread.h>
#include <sys/thread2.h>
#include <sys/sysctl.h>
#include <sys/spinlock2.h>
#include <machine/md_var.h>
#include <sys/ctype.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/msgport.h>
#include <sys/msgport2.h>
#include <sys/buf2.h>
#include <sys/dsched.h>
#include <machine/varargs.h>
#include <machine/param.h>

#include <dsched/fq/dsched_fq.h>

MALLOC_DECLARE(M_DSCHEDFQ);

static int	dsched_fq_version_maj = 0;
static int	dsched_fq_version_min = 7;

struct dsched_fq_stats	fq_stats;

struct objcache_malloc_args dsched_fq_dpriv_malloc_args = {
	sizeof(struct dsched_fq_dpriv), M_DSCHEDFQ };
struct objcache_malloc_args dsched_fq_priv_malloc_args = {
	sizeof(struct dsched_fq_priv), M_DSCHEDFQ };
struct objcache_malloc_args dsched_fq_mpriv_malloc_args = {
	sizeof(struct dsched_fq_mpriv), M_DSCHEDFQ };

static struct objcache	*fq_dpriv_cache;
static struct objcache	*fq_mpriv_cache;
static struct objcache	*fq_priv_cache;

TAILQ_HEAD(, dsched_fq_mpriv)	dsched_fqmp_list =
		TAILQ_HEAD_INITIALIZER(dsched_fqmp_list);

struct spinlock	fq_fqmp_lock;
struct callout	fq_callout;

extern struct dsched_ops dsched_fq_ops;

void db_stack_trace_cmd(int addr, boolean_t have_addr, int count,char *modif);
void fq_print_backtrace(void);

void
fq_print_backtrace(void)
{
	register_t  ebp;

	__asm __volatile("movl %%ebp, %0" : "=r" (ebp));
	db_stack_trace_cmd(ebp, 1, 4, NULL);
}

void
fq_reference_dpriv(struct dsched_fq_dpriv *dpriv)
{
	int refcount;

	refcount = atomic_fetchadd_int(&dpriv->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_reference_priv(struct dsched_fq_priv *fqp)
{
	int refcount;

	refcount = atomic_fetchadd_int(&fqp->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_reference_mpriv(struct dsched_fq_mpriv *fqmp)
{
	int refcount;

	refcount = atomic_fetchadd_int(&fqmp->refcount, 1);

	KKASSERT(refcount >= 0);
}

void
fq_dereference_dpriv(struct dsched_fq_dpriv *dpriv)
{
	struct dsched_fq_priv	*fqp, *fqp2;
	int refcount;

	refcount = atomic_fetchadd_int(&dpriv->refcount, -1);


	KKASSERT(refcount >= -3);

	if (refcount == 1) {
		atomic_subtract_int(&dpriv->refcount, 3); /* mark as: in destruction */
#if 1
		kprintf("dpriv (%p) destruction started, trace:\n", dpriv);
		fq_print_backtrace();
#endif
		spin_lock_wr(&dpriv->lock);
		TAILQ_FOREACH_MUTABLE(fqp, &dpriv->fq_priv_list, dlink, fqp2) {
			TAILQ_REMOVE(&dpriv->fq_priv_list, fqp, dlink);
			fqp->flags &= ~FQP_LINKED_DPRIV;
			fq_dereference_priv(fqp);
		}
		spin_unlock_wr(&dpriv->lock);

		objcache_put(fq_dpriv_cache, dpriv);
		atomic_subtract_int(&fq_stats.dpriv_allocations, 1);
	}
}

void
fq_dereference_priv(struct dsched_fq_priv *fqp)
{
	struct dsched_fq_mpriv	*fqmp;
	struct dsched_fq_dpriv	*dpriv;
	int refcount;

	refcount = atomic_fetchadd_int(&fqp->refcount, -1);

	KKASSERT(refcount >= -3);

	if (refcount == 1) {
		atomic_subtract_int(&fqp->refcount, 3); /* mark as: in destruction */
#if 0
		kprintf("fqp (%p) destruction started, trace:\n", fqp);
		fq_print_backtrace();
#endif
		dpriv = fqp->dpriv;
		KKASSERT(dpriv != NULL);

		spin_lock_wr(&fqp->lock);

		KKASSERT(fqp->qlength == 0);

		if (fqp->flags & FQP_LINKED_DPRIV) {
			spin_lock_wr(&dpriv->lock);

			TAILQ_REMOVE(&dpriv->fq_priv_list, fqp, dlink);
			fqp->flags &= ~FQP_LINKED_DPRIV;

			spin_unlock_wr(&dpriv->lock);
		}

		if (fqp->flags & FQP_LINKED_FQMP) {
			fqmp = fqp->fqmp;
			KKASSERT(fqmp != NULL);

			spin_lock_wr(&fqmp->lock);

			TAILQ_REMOVE(&fqmp->fq_priv_list, fqp, link);
			fqp->flags &= ~FQP_LINKED_FQMP;

			spin_unlock_wr(&fqmp->lock);
		}

		spin_unlock_wr(&fqp->lock);

		objcache_put(fq_priv_cache, fqp);
		atomic_subtract_int(&fq_stats.fqp_allocations, 1);
#if 0
		fq_dereference_dpriv(dpriv);
#endif
	}
}

void
fq_dereference_mpriv(struct dsched_fq_mpriv *fqmp)
{
	struct dsched_fq_priv	*fqp, *fqp2;
	int refcount;

	refcount = atomic_fetchadd_int(&fqmp->refcount, -1);

	KKASSERT(refcount >= -3);

	if (refcount == 1) {
		atomic_subtract_int(&fqmp->refcount, 3); /* mark as: in destruction */
#if 0
		kprintf("fqmp (%p) destruction started, trace:\n", fqmp);
		fq_print_backtrace();
#endif
		FQ_GLOBAL_FQMP_LOCK();
		spin_lock_wr(&fqmp->lock);

		TAILQ_FOREACH_MUTABLE(fqp, &fqmp->fq_priv_list, link, fqp2) {
			TAILQ_REMOVE(&fqmp->fq_priv_list, fqp, link);
			fqp->flags &= ~FQP_LINKED_FQMP;
			fq_dereference_priv(fqp);
		}
		TAILQ_REMOVE(&dsched_fqmp_list, fqmp, link);

		spin_unlock_wr(&fqmp->lock);
		FQ_GLOBAL_FQMP_UNLOCK();

		objcache_put(fq_mpriv_cache, fqmp);
		atomic_subtract_int(&fq_stats.fqmp_allocations, 1);
	}
}


struct dsched_fq_priv *
fq_alloc_priv(struct disk *dp)
{
	struct dsched_fq_priv	*fqp;
#if 0
	fq_reference_dpriv(dsched_get_disk_priv(dp));
#endif
	fqp = objcache_get(fq_priv_cache, M_WAITOK);
	bzero(fqp, sizeof(struct dsched_fq_priv));

	/* XXX: maybe we do need another ref for the disk list for fqp */
	fq_reference_priv(fqp);

	FQ_FQP_LOCKINIT(fqp);
	FQ_FQP_LOCK(fqp);
	fqp->dp = dp;

	fqp->dpriv = dsched_get_disk_priv(dp);

	TAILQ_INIT(&fqp->queue);
	TAILQ_INSERT_TAIL(&fqp->dpriv->fq_priv_list, fqp, dlink);
	fqp->flags |= FQP_LINKED_DPRIV;

	atomic_add_int(&fq_stats.fqp_allocations, 1);
	FQ_FQP_UNLOCK(fqp);
	return fqp;
}


struct dsched_fq_dpriv *
fq_alloc_dpriv(struct disk *dp)
{
	struct dsched_fq_dpriv *dpriv;

	dpriv = objcache_get(fq_dpriv_cache, M_WAITOK);
	bzero(dpriv, sizeof(struct dsched_fq_dpriv));
	fq_reference_dpriv(dpriv);
	dpriv->dp = dp;
	dpriv->avg_rq_time = 0;
	dpriv->incomplete_tp = 0;
	FQ_DPRIV_LOCKINIT(dpriv);
	TAILQ_INIT(&dpriv->fq_priv_list);

	atomic_add_int(&fq_stats.dpriv_allocations, 1);
	return dpriv;
}


struct dsched_fq_mpriv *
fq_alloc_mpriv()
{
	struct dsched_fq_mpriv	*fqmp;
	struct dsched_fq_priv	*fqp;
	struct disk	*dp = NULL;

	fqmp = objcache_get(fq_mpriv_cache, M_WAITOK);
	bzero(fqmp, sizeof(struct dsched_fq_mpriv));
	fq_reference_mpriv(fqmp);
#if 0
	kprintf("fq_alloc_mpriv, new fqmp = %p\n", fqmp);
#endif
	FQ_FQMP_LOCKINIT(fqmp);
	FQ_FQMP_LOCK(fqmp);
	TAILQ_INIT(&fqmp->fq_priv_list);

	while ((dp = dsched_disk_enumerate(dp, &dsched_fq_ops))) {
		fqp = fq_alloc_priv(dp);

#if 0
		fq_reference_priv(fqp);
#endif
		fqp->fqmp = fqmp;
		TAILQ_INSERT_TAIL(&fqmp->fq_priv_list, fqp, link);
		fqp->flags |= FQP_LINKED_FQMP;
	}

	FQ_GLOBAL_FQMP_LOCK();
	TAILQ_INSERT_TAIL(&dsched_fqmp_list, fqmp, link);
	FQ_GLOBAL_FQMP_UNLOCK();
	FQ_FQMP_UNLOCK(fqmp);

	atomic_add_int(&fq_stats.fqmp_allocations, 1);
	return fqmp;
}


void
fq_dispatcher(struct dsched_fq_dpriv *dpriv)
{
	struct dsched_fq_priv	*fqp, *fqp2;
	struct bio *bio, *bio2;
	int count;

	FQ_DPRIV_LOCK(dpriv);
	for(;;) {
		/* sleep ~60 ms */
		if (ssleep(dpriv, &dpriv->lock, 0, "fq_dispatcher", hz/15) == 0) {
			FQ_DPRIV_UNLOCK(dpriv);
			kprintf("fq_dispatcher is peacefully dying\n");
			lwkt_exit();
		}

		TAILQ_FOREACH_MUTABLE(fqp, &dpriv->fq_priv_list, dlink, fqp2) {
			if (fqp->qlength > 0) {
				FQ_FQP_LOCK(fqp);
				count = 0;

				TAILQ_FOREACH_MUTABLE(bio, &fqp->queue, link, bio2) {
					if ((fqp->max_tp > 0) &&
					    ((count >= fqp->max_tp) ||
					    (fqp->transactions >= fqp->max_tp)))
						break;
					TAILQ_REMOVE(&fqp->queue, bio, link);

					--fqp->qlength;
					KKASSERT(fqp != NULL);
					/*
					 * beware that we do have an fqp reference
					 * from the queueing
					 */
					dsched_strategy_async(dpriv->dp, bio,
					    fq_completed, fqp);
					atomic_add_int(&dpriv->incomplete_tp, 1);
					atomic_add_int(&fq_stats.transactions, 1);
					++count;
				}
				FQ_FQP_UNLOCK(fqp);
			}
		}
	}
}


void
fq_balance_thread(struct dsched_fq_dpriv *dpriv)
{
	struct	dsched_fq_priv	*fqp, *fqp2;
	int	n = 0;
	static int last_full = 0, prev_full = 0;
	int	incomplete_tp;
	int64_t total_budget, use_pct, avail_pct;
	total_budget = 0;

	FQ_DPRIV_LOCK(dpriv);
	incomplete_tp = dpriv->incomplete_tp;

	TAILQ_FOREACH_MUTABLE(fqp, &dpriv->fq_priv_list, dlink, fqp2) {
		if (fqp->transactions > 0 /* 30 */) {
			total_budget += (fqp->avg_latency * fqp->transactions);
			dsched_debug(LOG_INFO,
			    "%d) avg_latency = %d, transactions = %d\n",
			    n, fqp->avg_latency, fqp->transactions);
			++n;
		} else {
			fqp->max_tp = 0;
			fqp->avg_latency = 0;
		}
	}

	dsched_debug(LOG_INFO, "%d procs competing for disk\n"
	    "total_budget = %lld\n"
	    "incomplete tp = %d\n", n, total_budget, incomplete_tp);

	if (n == 0)
		goto done;

#if 0
	/*
	 * XXX: hack. don't know why total_budget can be zero here
	 * -> this doesn't apply anymore. total_budget is never 0 now
	 */
	if (total_budget == 0)
		total_budget = 1;
#endif

	TAILQ_FOREACH_MUTABLE(fqp, &dpriv->fq_priv_list, dlink, fqp2) {
		/* XXX: proportional to scheduler class! */
		avail_pct = (int64_t)1000/(int64_t)n;

		/* XXX: 100/(sum of scheduler priorities) * scheduler priority */
		/* XXX: but need to process queues of fqp on buckets or so...*/

		use_pct = ((int64_t)1000* (int64_t)fqp->avg_latency *
		    (int64_t)fqp->transactions)/(int64_t)total_budget;

		/* process is exceeding its fair share; rate-limit it */
		if ((use_pct > avail_pct) && (incomplete_tp > n*2)) {
			/* kprintf("here we are, use_pct > avail_pct\n"); */
			/* fqp->max_tp = avail_pct * fqp->avg_latency; */
			fqp->max_tp = total_budget/(n * fqp->avg_latency);
			dsched_debug(LOG_INFO,
			    "rate limited to %d transactions\n", fqp->max_tp);
			atomic_add_int(&fq_stats.procs_limited, 1);
		} else if (((use_pct < avail_pct/2) || (incomplete_tp < n*2)) &&
		    (!prev_full && !last_full)) {
			/*
			 * process is really using little of its timeslice, or the
			 * disk is not busy, so let's reset the rate-limit.
			 * Without this, exceeding processes will get an unlimited
			 * slice every other slice.
			 * XXX: this still doesn't quite fix the issue, but maybe,
			 * it's good that way so that heavy writes are interleaved.
			 */
			fqp->max_tp = 0;
		}
		fqp->transactions = 0;
		fqp->avg_latency = 0;
	}

	prev_full = last_full;
	last_full = (incomplete_tp > n*2)?1:0;

done:
	FQ_DPRIV_UNLOCK(dpriv);
	callout_reset(&fq_callout, hz * FQ_REBALANCE_TIMEOUT,
	    (void (*)(void *))fq_balance_thread, dpriv);
}


static int
do_fqstats(SYSCTL_HANDLER_ARGS)
{
	return (sysctl_handle_opaque(oidp, &fq_stats, sizeof(struct dsched_fq_stats), req));
}


SYSCTL_PROC(_kern, OID_AUTO, fq_stats, CTLTYPE_OPAQUE|CTLFLAG_RD,
    0, sizeof(struct dsched_fq_stats), do_fqstats, "fq_stats",
    "dsched_fq statistics");




static void
fq_init(void)
{

}

static void
fq_uninit(void)
{

}

static void
fq_earlyinit(void)
{
	fq_priv_cache = objcache_create("fq-priv-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_fq_priv_malloc_args );

	fq_mpriv_cache = objcache_create("fq-mpriv-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_fq_mpriv_malloc_args );

	FQ_GLOBAL_FQMP_LOCKINIT();

	fq_dpriv_cache = objcache_create("fq-dpriv-cache", 0, 0,
					   NULL, NULL, NULL,
					   objcache_malloc_alloc,
					   objcache_malloc_free,
					   &dsched_fq_dpriv_malloc_args );

	bzero(&fq_stats, sizeof(struct dsched_fq_stats));

	dsched_register(&dsched_fq_ops);
	callout_init_mp(&fq_callout);

	kprintf("FQ scheduler policy version %d.%d loaded\n",
	    dsched_fq_version_maj, dsched_fq_version_min);
}

static void
fq_earlyuninit(void)
{
	callout_stop(&fq_callout);
	callout_deactivate(&fq_callout);
	return;
}

SYSINIT(fq_register, SI_SUB_PRE_DRIVERS, SI_ORDER_ANY, fq_init, NULL);
SYSUNINIT(fq_register, SI_SUB_PRE_DRIVERS, SI_ORDER_FIRST, fq_uninit, NULL);

SYSINIT(fq_early, SI_SUB_CREATE_INIT-1, SI_ORDER_FIRST, fq_earlyinit, NULL);
SYSUNINIT(fq_early, SI_SUB_CREATE_INIT-1, SI_ORDER_ANY, fq_earlyuninit, NULL);
