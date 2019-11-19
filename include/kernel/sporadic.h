/*
 * Copyright 2019, Data61
 * Commonwealth Scientific and Industrial Research Organisation (CSIRO)
 * ABN 41 687 119 230.
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(DATA61_GPL)
 */
#ifndef __KERNEL_SPORADIC_H
#define __KERNEL_SPORADIC_H
/* This header presents the interface for sporadic servers,
 * implemented according to Stankcovich et. al in
 * "Defects of the POSIX Spoardic Server and How to correct them",
 * although without the priority management.
 *
 * Briefly, a sporadic server is a period and a queue of refills. Each
 * refill consists of an amount, and a period. No thread is allowed to consume
 * more than amount ticks per period.
 *
 * The sum of all refill amounts in the refill queue is always the budget of the scheduling context -
 * that is it should never change, unless it is being updated / configured.
 *
 * Every time budget is consumed, that amount of budget is scheduled
 * for reuse in period time. If the refill queue is full (the queue's
 * minimum size is 2, and can be configured by the user per scheduling context
 * above this) the next refill is merged.
 */
#include <config.h>
#include <types.h>
#include <util.h>
#include <object/structures.h>
#include <machine/timer.h>
#include <model/statedata.h>

/* To do an operation in the kernel, the thread must have
 * at least this much budget - see comment on refill_sufficient */
#define MIN_BUDGET_US (2u * getKernelWcetUs() * CONFIG_KERNEL_WCET_SCALE)
#define MIN_SC_BUDGET_US (2 * MIN_BUDGET_US)
#define MIN_BUDGET    (2u * getKernelWcetTicks() * CONFIG_KERNEL_WCET_SCALE)
#define MIN_SC_BUDGET (2 * MIN_BUDGET)

/* Short hand for accessing refill queue items */
#define REFILL_BUFFER(sc) ((refill_t *) (SC_REF(sc) + sizeof(sched_context_t)))
#define REFILL_INDEX(sc, index) (*refill_index(sc, index))
#define REFILL_HEAD(sc) REFILL_INDEX((sc), (sc)->scRefillHead)
#define REFILL_TAIL(sc) REFILL_INDEX((sc), refill_tail_index(sc))


/* Scheduling context objects consist of a sched_context_t at the start, followed by a
 * circular buffer of refills. As scheduling context objects are of variable size, the
 * amount of refill_ts that can fit into a scheduling context object is also variable.
 *
 * @return the maximum number of refill_t data structures that can fit into this
 * specific scheduling context object.
 */
static inline word_t refill_absolute_max(cap_t sc_cap)
{
    return (BIT(cap_sched_context_cap_get_capSCSizeBits(sc_cap)) - sizeof(sched_context_t)) / sizeof(refill_t);
}

/* @return the current amount of empty slots in the refill buffer */
static inline word_t refill_size(sched_context_t *sc)
{
    return sc->scRefillCount;
}

/* @return true if the circular buffer of refills is current full (all slots in the
 * buffer are currently being used */
static inline bool_t refill_full(sched_context_t *sc)
{
    return sc->scRefillCount == sc->scRefillMax;
}

static inline bool_t refill_empty(sched_context_t *sc)
{
    return sc->scRefillCount == 0;
}

static inline word_t refill_tail_index(sched_context_t *sc)
{
    assert(sc->scRefillHead <= sc->scRefillMax);
    assert(sc->scRefillCount <= sc->scRefillMax);
    assert(sc->scRefillCount >= 1);

    word_t index = sc->scRefillHead + sc->scRefillCount - 1;

    if (index >= sc->scRefillMax) {
        index -= sc->scRefillMax;
    }

    assert(index < sc->scRefillMax);
    return index;
}

static inline UNUSED bool_t index_valid(sched_context_t *sc, word_t index)
{
    assert(sc->scRefillHead <= sc->scRefillMax);
    assert(sc->scRefillCount <= sc->scRefillMax);

    if (sc->scRefillHead + sc->scRefillCount > sc->scRefillMax) {
        /* Discontiguous allocations */
        if (index < sc->scRefillHead + sc->scRefillCount - sc->scRefillMax) {
            /* Before the tail */
            return true;
        } else if (sc->scRefillHead <= index) {
            /* After the head */
            return true;
        } else {
            return false;
        }
    } else {
        /* Contiguous allocations */
        if (index < sc->scRefillHead) {
            /* Before the head */
            return false;
        } else if (sc->scRefillHead + sc->scRefillCount < index) {
            /* After the tail */
            return false;
        } else {
            return true;
        }
    }
}

static inline refill_t *refill_index(sched_context_t *sc, word_t index)
{
    assert(!refill_empty(sc));
    assert(index < sc->scRefillMax);
    assert(index_valid(sc, index));
    return &REFILL_BUFFER(sc)[index];
}

/* Return the amount of budget this scheduling context
 * has available if usage is charged to it. */
static inline ticks_t refill_capacity(sched_context_t *sc, ticks_t usage)
{
    assert(!refill_empty(sc));

    if (unlikely(usage > REFILL_HEAD(sc).rAmount)) {
        return 0;
    }

    return REFILL_HEAD(sc).rAmount - usage;
}

/*
 * Return true if the head refill has sufficient capacity
 * to enter and exit the kernel after usage is charged to it.
 */
static inline bool_t refill_sufficient(sched_context_t *sc, ticks_t usage)
{
    assert(!refill_empty(sc));
    return refill_capacity(sc, usage) >= MIN_BUDGET;
}

/*
 * Return true if the head refill is eligible to be used.
 * This indicates if the thread bound to the sc can be placed
 * into the scheduler, otherwise it needs to go into the release queue
 * to wait.
 */
static inline bool_t refill_ready(sched_context_t *sc)
{
    assert(!refill_empty(sc));
    return REFILL_HEAD(sc).rTime <= (NODE_STATE(ksCurTime) + getKernelWcetTicks());
}

/*
 * Return true if an SC has been successfully configured with parameters
 * that allow for a thread to run.
 */
static inline bool_t sc_active(sched_context_t *sc)
{
    return sc->scRefillMax > 0;
}

/* Create a new refill in a non-active sc */
void refill_new(sched_context_t *sc, word_t max_refills, ticks_t budget, ticks_t period);

/* Update refills in an active sc without violating bandwidth constraints */
void refill_update(sched_context_t *sc, ticks_t new_period, ticks_t new_budget, word_t new_max_refills);

/* Charge `usage` to the current scheduling context.
 *
 * @param usage the amount of time to charge.
 * @param capacity the value returned by refill_capacity. At most call sites this
 * has already been calculated so pass the value in rather than calculating it again.
 */
void refill_budget_check(ticks_t used);

/* Charge `usage` to the current scheduling context (round-robin).
 *
 * @param usage the amount of time to charge.
 */
void refill_budget_check_round_robin(ticks_t usage);

/*
 * This is called when a thread is eligible to start running: it
 * iterates through the refills queue and merges any
 * refills that overlap.
 */
void refill_unblock_check(sched_context_t *sc);

#endif
