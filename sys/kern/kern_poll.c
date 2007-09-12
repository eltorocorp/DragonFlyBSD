/*-
 * Copyright (c) 2001-2002 Luigi Rizzo
 *
 * Supported by: the Xorp Project (www.xorp.org)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/kern/kern_poll.c,v 1.2.2.4 2002/06/27 23:26:33 luigi Exp $
 * $DragonFly: src/sys/kern/kern_poll.c,v 1.33 2007/09/12 12:02:09 sephe Exp $
 */

#include "opt_polling.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/socket.h>			/* needed by net/if.h		*/
#include <sys/sysctl.h>

#include <sys/thread2.h>
#include <sys/msgport2.h>

#include <net/if.h>			/* for IFF_* flags		*/
#include <net/netisr.h>			/* for NETISR_POLL		*/

/*
 * Polling support for [network] device drivers.
 *
 * Drivers which support this feature try to register with the
 * polling code.
 *
 * If registration is successful, the driver must disable interrupts,
 * and further I/O is performed through the handler, which is invoked
 * (at least once per clock tick) with 3 arguments: the "arg" passed at
 * register time (a struct ifnet pointer), a command, and a "count" limit.
 *
 * The command can be one of the following:
 *  POLL_ONLY: quick move of "count" packets from input/output queues.
 *  POLL_AND_CHECK_STATUS: as above, plus check status registers or do
 *	other more expensive operations. This command is issued periodically
 *	but less frequently than POLL_ONLY.
 *  POLL_DEREGISTER: deregister and return to interrupt mode.
 *  POLL_REGISTER: register and disable interrupts
 *
 * The first two commands are only issued if the interface is marked as
 * 'IFF_UP, IFF_RUNNING and IFF_POLLING', the last two only if IFF_RUNNING
 * is set.
 *
 * The count limit specifies how much work the handler can do during the
 * call -- typically this is the number of packets to be received, or
 * transmitted, etc. (drivers are free to interpret this number, as long
 * as the max time spent in the function grows roughly linearly with the
 * count).
 *
 * Deregistration can be requested by the driver itself (typically in the
 * *_stop() routine), or by the polling code, by invoking the handler.
 *
 * Polling can be globally enabled or disabled with the sysctl variable
 * kern.polling.enable (default is 0, disabled)
 *
 * A second variable controls the sharing of CPU between polling/kernel
 * network processing, and other activities (typically userlevel tasks):
 * kern.polling.user_frac (between 0 and 100, default 50) sets the share
 * of CPU allocated to user tasks. CPU is allocated proportionally to the
 * shares, by dynamically adjusting the "count" (poll_burst).
 *
 * Other parameters can should be left to their default values.
 * The following constraints hold
 *
 *	1 <= poll_each_burst <= poll_burst <= poll_burst_max
 *	MIN_POLL_BURST_MAX <= poll_burst_max <= MAX_POLL_BURST_MAX
 */

#define MIN_POLL_BURST_MAX	10
#define MAX_POLL_BURST_MAX	1000

#ifndef DEVICE_POLLING_FREQ_MAX
#define DEVICE_POLLING_FREQ_MAX		30000
#endif
#define DEVICE_POLLING_FREQ_DEFAULT	2000

#define POLL_LIST_LEN  128
struct pollrec {
	struct ifnet	*ifp;
};

#define POLLCTX_MAX	32

struct pollctx {
	struct sysctl_ctx_list	poll_sysctl_ctx;
	struct sysctl_oid	*poll_sysctl_tree;

	uint32_t		poll_burst;
	uint32_t		poll_each_burst;
	uint32_t		poll_burst_max;
	uint32_t		user_frac;
	int			reg_frac_count;
	uint32_t		reg_frac;
	uint32_t		short_ticks;
	uint32_t		lost_polls;
	uint32_t		pending_polls;
	int			residual_burst;
	uint32_t		phase;
	uint32_t		suspect;
	uint32_t		stalled;
	struct timeval		poll_start_t;
	struct timeval		prev_t;

	uint32_t		poll_handlers; /* next free entry in pr[]. */
	struct pollrec		pr[POLL_LIST_LEN];

	int			poll_cpuid;
	struct systimer		pollclock;
	int			polling_enabled;
	int			pollhz;
};

static struct pollctx	*poll_context[POLLCTX_MAX];

SYSCTL_NODE(_kern, OID_AUTO, polling, CTLFLAG_RW, 0,
	"Device polling parameters");

static int	poll_defcpu = -1;
SYSCTL_INT(_kern_polling, OID_AUTO, defcpu, CTLFLAG_RD,
	&poll_defcpu, 0, "default CPU# to run device polling");

static uint32_t	poll_cpumask = 0x1;
#ifdef notyet
TUNABLE_INT("kern.polling.cpumask", &poll_cpumask);
#endif

static int	polling_enabled = 0;	/* global polling enable */
TUNABLE_INT("kern.polling.enable", &polling_enabled);

static int	pollhz = DEVICE_POLLING_FREQ_DEFAULT;
TUNABLE_INT("kern.polling.pollhz", &pollhz);

/* The two netisr handlers */
static void	netisr_poll(struct netmsg *);
static void	netisr_pollmore(struct netmsg *);

/* Systimer handler */
static void	pollclock(systimer_t, struct intrframe *);

/* Sysctl handlers */
static int	sysctl_pollhz(SYSCTL_HANDLER_ARGS);
static int	sysctl_polling(SYSCTL_HANDLER_ARGS);
static void	poll_add_sysctl(struct sysctl_ctx_list *,
				struct sysctl_oid_list *, struct pollctx *);

void		init_device_poll(void);		/* init routine */
void		init_device_poll_pcpu(int);	/* per-cpu init routine */

/*
 * register relevant netisr. Called from kern_clock.c:
 */
void
init_device_poll(void)
{
	netisr_register(NETISR_POLL, cpu0_portfn, netisr_poll);
	netisr_register(NETISR_POLLMORE, cpu0_portfn, netisr_pollmore);
}

void
init_device_poll_pcpu(int cpuid)
{
	struct pollctx *pctx;
	char cpuid_str[3];

	if (((1 << cpuid) & poll_cpumask) == 0)
		return;

	pctx = kmalloc(sizeof(*pctx), M_DEVBUF, M_WAITOK | M_ZERO);

	pctx->poll_burst = 5;
	pctx->poll_each_burst = 5;
	pctx->poll_burst_max = 150; /* good for 100Mbit net and HZ=1000 */
	pctx->user_frac = 50;
	pctx->reg_frac = 20;
	pctx->polling_enabled = polling_enabled;
	pctx->pollhz = pollhz;
	pctx->poll_cpuid = cpuid;

	KASSERT(cpuid < POLLCTX_MAX, ("cpu id must < %d", cpuid));
	poll_context[cpuid] = pctx;

	if (poll_defcpu < 0) {
		poll_defcpu = cpuid;

		/*
		 * Initialize global sysctl nodes, for compat
		 */
		poll_add_sysctl(NULL, SYSCTL_STATIC_CHILDREN(_kern_polling),
				pctx);
	}

	/*
	 * Initialize per-cpu sysctl nodes
	 */
	ksnprintf(cpuid_str, sizeof(cpuid_str), "%d", pctx->poll_cpuid);

	sysctl_ctx_init(&pctx->poll_sysctl_ctx);
	pctx->poll_sysctl_tree = SYSCTL_ADD_NODE(&pctx->poll_sysctl_ctx,
				 SYSCTL_STATIC_CHILDREN(_kern_polling),
				 OID_AUTO, cpuid_str, CTLFLAG_RD, 0, "");
	poll_add_sysctl(&pctx->poll_sysctl_ctx,
			SYSCTL_CHILDREN(pctx->poll_sysctl_tree), pctx);

	/*
	 * Initialize systimer
	 */
	systimer_init_periodic_nq(&pctx->pollclock, pollclock, pctx,
				  pctx->polling_enabled ? pctx->pollhz : 1);
}

/*
 * Set the polling frequency
 */
static int
sysctl_pollhz(SYSCTL_HANDLER_ARGS)
{
	struct pollctx *pctx = arg1;
	int error, phz;

	phz = pctx->pollhz;
	error = sysctl_handle_int(oidp, &phz, 0, req);
	if (error || req->newptr == NULL)
		return error;
	if (phz <= 0)
		return EINVAL;
	else if (phz > DEVICE_POLLING_FREQ_MAX)
		phz = DEVICE_POLLING_FREQ_MAX;

	crit_enter();
	pctx->pollhz = phz;
	if (pctx->polling_enabled)
		systimer_adjust_periodic(&pctx->pollclock, phz);
	crit_exit();
	return 0;
}

/*
 * Master enable.  If polling is disabled, cut the polling systimer 
 * frequency to 1hz.
 */
static int
sysctl_polling(SYSCTL_HANDLER_ARGS)
{
	struct pollctx *pctx = arg1;
	int error, enabled;

	enabled = pctx->polling_enabled;
	error = sysctl_handle_int(oidp, &enabled, 0, req);
	if (error || req->newptr == NULL)
		return error;

	crit_enter();
	pctx->polling_enabled = enabled;
	if (pctx->polling_enabled)
		systimer_adjust_periodic(&pctx->pollclock, pollhz);
	else
		systimer_adjust_periodic(&pctx->pollclock, 1);
	crit_exit();
	return 0;
}

/*
 * Hook from hardclock. Tries to schedule a netisr, but keeps track
 * of lost ticks due to the previous handler taking too long.
 * Normally, this should not happen, because polling handler should
 * run for a short time. However, in some cases (e.g. when there are
 * changes in link status etc.) the drivers take a very long time
 * (even in the order of milliseconds) to reset and reconfigure the
 * device, causing apparent lost polls.
 *
 * The first part of the code is just for debugging purposes, and tries
 * to count how often hardclock ticks are shorter than they should,
 * meaning either stray interrupts or delayed events.
 *
 * WARNING! called from fastint or IPI, the MP lock might not be held.
 */
static void
pollclock(systimer_t info, struct intrframe *frame __unused)
{
	struct pollctx *pctx = info->data;
	struct timeval t;
	int delta;

	if (pctx->poll_handlers == 0)
		return;

	microuptime(&t);
	delta = (t.tv_usec - pctx->prev_t.tv_usec) +
		(t.tv_sec - pctx->prev_t.tv_sec)*1000000;
	if (delta * hz < 500000)
		pctx->short_ticks++;
	else
		pctx->prev_t = t;

	if (pctx->pending_polls > 100) {
		/*
		 * Too much, assume it has stalled (not always true
		 * see comment above).
		 */
		pctx->stalled++;
		pctx->pending_polls = 0;
		pctx->phase = 0;
	}

	if (pctx->phase <= 2) {
		if (pctx->phase != 0)
			pctx->suspect++;
		pctx->phase = 1;
		schednetisr(NETISR_POLL);
		pctx->phase = 2;
	}
	if (pctx->pending_polls++ > 0)
		pctx->lost_polls++;
}

/*
 * netisr_pollmore is called after other netisr's, possibly scheduling
 * another NETISR_POLL call, or adapting the burst size for the next cycle.
 *
 * It is very bad to fetch large bursts of packets from a single card at once,
 * because the burst could take a long time to be completely processed, or
 * could saturate the intermediate queue (ipintrq or similar) leading to
 * losses or unfairness. To reduce the problem, and also to account better for
 * time spent in network-related processing, we split the burst in smaller
 * chunks of fixed size, giving control to the other netisr's between chunks.
 * This helps in improving the fairness, reducing livelock (because we
 * emulate more closely the "process to completion" that we have with
 * fastforwarding) and accounting for the work performed in low level
 * handling and forwarding.
 */

/* ARGSUSED */
static void
netisr_pollmore(struct netmsg *msg)
{
	struct pollctx *pctx;
	struct timeval t;
	int kern_load, cpuid;

	cpuid = mycpu->gd_cpuid;
	KKASSERT(cpuid < POLLCTX_MAX);

	pctx = poll_context[cpuid];
	KKASSERT(pctx != NULL);
	KKASSERT(pctx->poll_cpuid == cpuid);

	crit_enter();
	lwkt_replymsg(&msg->nm_lmsg, 0);
	pctx->phase = 5;
	if (pctx->residual_burst > 0) {
		schednetisr(NETISR_POLL);
		/* will run immediately on return, followed by netisrs */
		goto out;
	}
	/* here we can account time spent in netisr's in this tick */
	microuptime(&t);
	kern_load = (t.tv_usec - pctx->poll_start_t.tv_usec) +
		(t.tv_sec - pctx->poll_start_t.tv_sec)*1000000;	/* us */
	kern_load = (kern_load * hz) / 10000;			/* 0..100 */
	if (kern_load > (100 - pctx->user_frac)) { /* try decrease ticks */
		if (pctx->poll_burst > 1)
			pctx->poll_burst--;
	} else {
		if (pctx->poll_burst < pctx->poll_burst_max)
			pctx->poll_burst++;
	}

	pctx->pending_polls--;
	if (pctx->pending_polls == 0) {	/* we are done */
		pctx->phase = 0;
	} else {
		/*
		 * Last cycle was long and caused us to miss one or more
		 * hardclock ticks. Restart processing again, but slightly
		 * reduce the burst size to prevent that this happens again.
		 */
		pctx->poll_burst -= (pctx->poll_burst / 8);
		if (pctx->poll_burst < 1)
			pctx->poll_burst = 1;
		schednetisr(NETISR_POLL);
		pctx->phase = 6;
	}
out:
	crit_exit();
}

/*
 * netisr_poll is scheduled by schednetisr when appropriate, typically once
 * per tick.
 *
 * Note that the message is replied immediately in order to allow a new
 * ISR to be scheduled in the handler.
 *
 * XXX each registration should indicate whether it needs a critical
 * section to operate.
 */
/* ARGSUSED */
static void
netisr_poll(struct netmsg *msg)
{
	struct pollctx *pctx;
	int i, cycles, cpuid;
	enum poll_cmd arg = POLL_ONLY;

	cpuid = mycpu->gd_cpuid;
	KKASSERT(cpuid < POLLCTX_MAX);

	pctx = poll_context[cpuid];
	KKASSERT(pctx != NULL);
	KKASSERT(pctx->poll_cpuid == cpuid);

	lwkt_replymsg(&msg->nm_lmsg, 0);
	crit_enter();
	pctx->phase = 3;
	if (pctx->residual_burst == 0) { /* first call in this tick */
		microuptime(&pctx->poll_start_t);
		/*
		 * Check that paremeters are consistent with runtime
		 * variables. Some of these tests could be done at sysctl
		 * time, but the savings would be very limited because we
		 * still have to check against reg_frac_count and
		 * poll_each_burst. So, instead of writing separate sysctl
		 * handlers, we do all here.
		 */

		if (pctx->reg_frac > hz)
			pctx->reg_frac = hz;
		else if (pctx->reg_frac < 1)
			pctx->reg_frac = 1;
		if (pctx->reg_frac_count > pctx->reg_frac)
			pctx->reg_frac_count = pctx->reg_frac - 1;
		if (pctx->reg_frac_count-- == 0) {
			arg = POLL_AND_CHECK_STATUS;
			pctx->reg_frac_count = pctx->reg_frac - 1;
		}
		if (pctx->poll_burst_max < MIN_POLL_BURST_MAX)
			pctx->poll_burst_max = MIN_POLL_BURST_MAX;
		else if (pctx->poll_burst_max > MAX_POLL_BURST_MAX)
			pctx->poll_burst_max = MAX_POLL_BURST_MAX;

		if (pctx->poll_each_burst < 1)
			pctx->poll_each_burst = 1;
		else if (pctx->poll_each_burst > pctx->poll_burst_max)
			pctx->poll_each_burst = pctx->poll_burst_max;

		pctx->residual_burst = pctx->poll_burst;
	}
	cycles = (pctx->residual_burst < pctx->poll_each_burst) ?
		pctx->residual_burst : pctx->poll_each_burst;
	pctx->residual_burst -= cycles;

	if (pctx->polling_enabled) {
		for (i = 0 ; i < pctx->poll_handlers ; i++) {
			struct ifnet *ifp = pctx->pr[i].ifp;

			if ((ifp->if_flags & (IFF_UP|IFF_RUNNING|IFF_POLLING))
			    == (IFF_UP|IFF_RUNNING|IFF_POLLING)) {
				if (lwkt_serialize_try(ifp->if_serializer)) {
					ifp->if_poll(ifp, arg, cycles);
					lwkt_serialize_exit(ifp->if_serializer);
				}
			}
		}
	} else {	/* unregister */
		for (i = 0 ; i < pctx->poll_handlers ; i++) {
			struct ifnet *ifp = pctx->pr[i].ifp;

			if ((ifp->if_flags & IFF_POLLING) == 0)
				continue;
			/*
			 * Only call the interface deregistration
			 * function if the interface is still 
			 * running.
			 */
			lwkt_serialize_enter(ifp->if_serializer);
			ifp->if_flags &= ~IFF_POLLING;
			if (ifp->if_flags & IFF_RUNNING)
				ifp->if_poll(ifp, POLL_DEREGISTER, 1);
			lwkt_serialize_exit(ifp->if_serializer);
		}
		pctx->residual_burst = 0;
		pctx->poll_handlers = 0;
	}
	schednetisr(NETISR_POLLMORE);
	pctx->phase = 4;
	crit_exit();
}

/*
 * Try to register routine for polling. Returns 1 if successful
 * (and polling should be enabled), 0 otherwise.
 *
 * Called from mainline code only, not called from an interrupt.
 */
int
ether_poll_register(struct ifnet *ifp)
{
	struct pollctx *pctx;
	int rc;

	if (poll_defcpu < 0)
		return 0;
	KKASSERT(poll_defcpu < POLLCTX_MAX);

	pctx = poll_context[poll_defcpu];
	KKASSERT(pctx != NULL);
	KKASSERT(pctx->poll_cpuid == poll_defcpu);

	if (pctx->polling_enabled == 0) /* polling disabled, cannot register */
		return 0;
	if (ifp->if_flags & IFF_POLLING)	/* already polling	*/
		return 0;
	if (ifp->if_poll == NULL)		/* no polling support   */
		return 0;

	/*
	 * Attempt to register.  Interlock with IFF_POLLING.
	 */
	crit_enter();	/* XXX MP - not mp safe */
	lwkt_serialize_enter(ifp->if_serializer);
	ifp->if_flags |= IFF_POLLING;
	if (ifp->if_flags & IFF_RUNNING)
		ifp->if_poll(ifp, POLL_REGISTER, 0);
	lwkt_serialize_exit(ifp->if_serializer);
	if ((ifp->if_flags & IFF_POLLING) == 0) {
		crit_exit();
		return 0;
	}

	/*
	 * Check if there is room.  If there isn't, deregister.
	 */
	if (pctx->poll_handlers >= POLL_LIST_LEN) {
		/*
		 * List full, cannot register more entries.
		 * This should never happen; if it does, it is probably a
		 * broken driver trying to register multiple times. Checking
		 * this at runtime is expensive, and won't solve the problem
		 * anyways, so just report a few times and then give up.
		 */
		static int verbose = 10;	/* XXX */
		if (verbose >0) {
			kprintf("poll handlers list full, "
				"maybe a broken driver ?\n");
			verbose--;
		}
		lwkt_serialize_enter(ifp->if_serializer);
		ifp->if_flags &= ~IFF_POLLING;
		if (ifp->if_flags & IFF_RUNNING)
			ifp->if_poll(ifp, POLL_DEREGISTER, 0);
		lwkt_serialize_exit(ifp->if_serializer);
		rc = 0;
	} else {
		pctx->pr[pctx->poll_handlers].ifp = ifp;
		pctx->poll_handlers++;
		rc = 1;
	}
	crit_exit();
	return (rc);
}

/*
 * Remove interface from the polling list.  Occurs when polling is turned
 * off.  Called from mainline code only, not called from an interrupt.
 */
int
ether_poll_deregister(struct ifnet *ifp)
{
	struct pollctx *pctx;
	int i;

	KKASSERT(ifp != NULL);

	if (poll_defcpu < 0)
		return 0;
	KKASSERT(poll_defcpu < POLLCTX_MAX);

	pctx = poll_context[poll_defcpu];
	KKASSERT(pctx != NULL);
	KKASSERT(pctx->poll_cpuid == poll_defcpu);

	crit_enter();
	if ((ifp->if_flags & IFF_POLLING) == 0) {
		crit_exit();
		return 0;
	}
	for (i = 0 ; i < pctx->poll_handlers ; i++) {
		if (pctx->pr[i].ifp == ifp) /* found it */
			break;
	}
	ifp->if_flags &= ~IFF_POLLING; /* found or not... */
	if (i == pctx->poll_handlers) {
		crit_exit();
		kprintf("ether_poll_deregister: ifp not found!!!\n");
		return 0;
	}
	pctx->poll_handlers--;
	if (i < pctx->poll_handlers) { /* Last entry replaces this one. */
		pctx->pr[i].ifp = pctx->pr[pctx->poll_handlers].ifp;
	}
	crit_exit();

	/*
	 * Only call the deregistration function if the interface is still
	 * in a run state.
	 */
	if (ifp->if_flags & IFF_RUNNING) {
		lwkt_serialize_enter(ifp->if_serializer);
		ifp->if_poll(ifp, POLL_DEREGISTER, 1);
		lwkt_serialize_exit(ifp->if_serializer);
	}
	return (1);
}

static void
poll_add_sysctl(struct sysctl_ctx_list *ctx, struct sysctl_oid_list *parent,
		struct pollctx *pctx)
{
	SYSCTL_ADD_PROC(ctx, parent, OID_AUTO, "enable",
			CTLTYPE_INT | CTLFLAG_RW, pctx, 0, sysctl_polling,
			"I", "Polling enabled");

	SYSCTL_ADD_PROC(ctx, parent, OID_AUTO, "pollhz",
			CTLTYPE_INT | CTLFLAG_RW, pctx, 0, sysctl_pollhz,
			"I", "Device polling frequency");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "phase", CTLFLAG_RD,
			&pctx->phase, 0, "Polling phase");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "suspect", CTLFLAG_RW,
			&pctx->suspect, 0, "suspect event");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "stalled", CTLFLAG_RW,
			&pctx->stalled, 0, "potential stalls");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "burst", CTLFLAG_RW,
			&pctx->poll_burst, 0, "Current polling burst size");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "each_burst", CTLFLAG_RW,
			&pctx->poll_each_burst, 0, "Max size of each burst");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "burst_max", CTLFLAG_RW,
			&pctx->poll_burst_max, 0, "Max Polling burst size");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "user_frac", CTLFLAG_RW,
			&pctx->user_frac, 0,
			"Desired user fraction of cpu time");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "reg_frac", CTLFLAG_RW,
			&pctx->reg_frac, 0,
			"Every this many cycles poll register");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "short_ticks", CTLFLAG_RW,
			&pctx->short_ticks, 0,
			"Hardclock ticks shorter than they should be");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "lost_polls", CTLFLAG_RW,
			&pctx->lost_polls, 0,
			"How many times we would have lost a poll tick");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "pending_polls", CTLFLAG_RD,
			&pctx->pending_polls, 0, "Do we need to poll again");

	SYSCTL_ADD_INT(ctx, parent, OID_AUTO, "residual_burst", CTLFLAG_RW,
		       &pctx->residual_burst, 0,
		       "# of residual cycles in burst");

	SYSCTL_ADD_UINT(ctx, parent, OID_AUTO, "handlers", CTLFLAG_RD,
			&pctx->poll_handlers, 0,
			"Number of registered poll handlers");
}
