/*
 * LWKT_RWLOCK.C (MP SAFE)
 *
 * Copyright (c) 2003,2004 The DragonFly Project.  All rights reserved.
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
 * Implements simple shared/exclusive locks using LWKT. 
 *
 * $DragonFly: src/sys/kern/Attic/lwkt_rwlock.c,v 1.9 2006/05/18 16:25:19 dillon Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/spinlock.h>
#include <sys/proc.h>
#include <sys/rtprio.h>
#include <sys/queue.h>
#include <sys/thread2.h>
#include <sys/spinlock2.h>

/*
 * lwkt_rwlock_init() (MP SAFE)
 *
 * NOTE! called from low level boot, we cannot do anything fancy.
 */
void
lwkt_rwlock_init(lwkt_rwlock_t lock)
{
    lwkt_wait_init(&lock->rw_wait);
    lock->rw_owner = NULL;
    lock->rw_count = 0;
    lock->rw_requests = 0;
}

/*
 * lwkt_rwlock_uninit() (MP SAFE)
 */
void
lwkt_rwlock_uninit(lwkt_rwlock_t lock)
{
    /* empty */
}

/*
 * lwkt_exlock() (MP SAFE)
 *
 * NOTE: We need to use a critical section in addition to the token to
 * interlock against IPI lwkt_schedule calls which may manipulate the
 * rw_wait structure's list.  This is because the IPI runs in the context of
 * the current thread and thus cannot use any token calls (if it did the
 * token would just share with the thread's token and not provide any 
 * protection).  This needs a rewrite.
 */
void
lwkt_exlock(lwkt_rwlock_t lock, const char *wmesg)
{
    int gen;

    spin_lock(&lock->rw_spinlock);
    gen = lock->rw_wait.wa_gen;
    while (lock->rw_owner != curthread) {
	if (lock->rw_owner == NULL && lock->rw_count == 0) {
	    lock->rw_owner = curthread;
	    break;
	}
	++lock->rw_requests;
	spin_unlock(&lock->rw_spinlock);
	lwkt_block(&lock->rw_wait, wmesg, &gen);
	spin_lock(&lock->rw_spinlock);
	--lock->rw_requests;
    }
    ++lock->rw_count;
    spin_unlock(&lock->rw_spinlock);
}

/*
 * lwkt_shlock() (MP SAFE)
 */
void
lwkt_shlock(lwkt_rwlock_t lock, const char *wmesg)
{
    int gen;

    spin_lock(&lock->rw_spinlock);
    gen = lock->rw_wait.wa_gen;
    while (lock->rw_owner != NULL) {
	++lock->rw_requests;
	spin_unlock(&lock->rw_spinlock);
	lwkt_block(&lock->rw_wait, wmesg, &gen);
	spin_lock(&lock->rw_spinlock);
	--lock->rw_requests;
    }
    ++lock->rw_count;
    spin_unlock(&lock->rw_spinlock);
}

/*
 * lwkt_exunlock() (MP SAFE)
 */
void
lwkt_exunlock(lwkt_rwlock_t lock)
{
    KASSERT(lock->rw_owner != NULL, ("lwkt_exunlock: shared lock"));
    KASSERT(lock->rw_owner == curthread, ("lwkt_exunlock: not owner"));
    spin_lock(&lock->rw_spinlock);
    if (--lock->rw_count == 0) {
	lock->rw_owner = NULL;
	if (lock->rw_requests) {
	    spin_unlock(&lock->rw_spinlock);
	    lwkt_signal(&lock->rw_wait, 1);
	    return;
	}
    }
    spin_unlock(&lock->rw_spinlock);
}

/*
 * lwkt_shunlock() (MP SAFE)
 */
void
lwkt_shunlock(lwkt_rwlock_t lock)
{
    KASSERT(lock->rw_owner == NULL, ("lwkt_shunlock: exclusive lock"));
    spin_lock(&lock->rw_spinlock);
    if (--lock->rw_count == 0) {
	if (lock->rw_requests) {
	    spin_unlock(&lock->rw_spinlock);
	    lwkt_signal(&lock->rw_wait, 1);
	    return;
	}
    }
    spin_unlock(&lock->rw_spinlock);
}

