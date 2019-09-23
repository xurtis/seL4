/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <types.h>
#include <api/failures.h>
#include <object/structures.h>

/* functions to manage the circular buffer of
 * sporadic budget replenishments (refills for short).
 *
 * The circular buffer always has at least one item in it.
 *
 * Items are appended at the tail (the back) and
 * removed from the head (the front). Below is
 * an example of a queue with 4 items (h = head, t = tail, x = item, [] = slot)
 * and max size 8.
 *
 * [][h][x][x][t][][][]
 *
 * and another example of a queue with 5 items
 *
 * [x][t][][][][h][x][x]
 *
 * The queue has a minimum size of 1, so it is possible that h == t.
 */

/* return the index of the next item in the refill queue */
static inline word_t refill_next(sched_context_t *sc, word_t index)
{
    return (index == sc->scRefillMax - 1u) ? (0) : index + 1u;
}

#ifdef CONFIG_PRINTING
/* for debugging */
UNUSED static inline void print_index(sched_context_t *sc, word_t index)
{

    printf("index %lu, Amount: %llx, time %llx\n", index, REFILL_INDEX(sc, index).rAmount,
           REFILL_INDEX(sc, index).rTime);
}

UNUSED static inline void refill_print(sched_context_t *sc)
{
    printf("Head %lu length %lu\n", sc->scRefillHead, sc->scRefillCount);
    printf("Budget %lu Period %lu\n", (long)sc->scBudget, (long)sc->scPeriod);

    word_t current = sc->scRefillHead;
    word_t seen = 0;
    while (seen != sc->scRefillCount) {
        print_index(sc, current);
        current = refill_next(sc, current);
        seen += 1;
    }

}
#endif /* CONFIG_PRINTING */
#ifdef CONFIG_DEBUG_BUILD
/* INVARIANTS */

/* Each refill ends before or at the time the subsequent refill
 * starts (the refills are in order and disjoint). */
static UNUSED bool_t refill_ordered_disjoint(sched_context_t *sc)
{
    word_t current = sc->scRefillHead;
    word_t next = refill_next(sc, sc->scRefillHead);
    word_t seen = 1;

    while (seen < sc->scRefillCount) {
        if (!(REFILL_INDEX(sc, current).rTime + REFILL_INDEX(sc, current).rAmount <= REFILL_INDEX(sc, next).rTime)) {
            refill_print(sc);
            return false;
        }
        current = next;
        next = refill_next(sc, current);
        seen += 1;
    }

    return true;
}

/* Each refill has at least MIN_BUDGET in its rAmount. */
static UNUSED bool_t refill_at_least_min_budget(sched_context_t *sc)
{
    word_t current = sc->scRefillHead;
    word_t seen = 0;

    while (seen < sc->scRefillCount) {
        if (!(REFILL_INDEX(sc, current).rAmount >= MIN_BUDGET)) {
            refill_print(sc);
            return false;
        }
        current = refill_next(sc, current);
        seen += 1;
    }

    return true;
}

static UNUSED ticks_t refill_sum(sched_context_t *sc);

/* The refills of a SC sum to exactly its budget. */
static UNUSED bool_t refill_sum_to_budget(sched_context_t *sc)
{
    if (!(refill_sum(sc) == sc->scBudget)) {
        refill_print(sc);
        return false;
    }

    return true;
}

/* All refills including refill amount occur within the window of a
 * single period. */
static UNUSED bool_t refill_all_within_period(sched_context_t *sc)
{
    if (!(REFILL_TAIL(sc).rTime + REFILL_TAIL(sc).rAmount - REFILL_HEAD(sc).rTime <= sc->scPeriod)) {
        refill_print(sc);
        return false;
    }
    return true;
}

static UNUSED void sched_invariants(sched_context_t *sc)
{
    assert(!refill_empty(sc));
    assert(sc->scBudget >= MIN_SC_BUDGET);
    assert(refill_ordered_disjoint(sc));
    assert(refill_at_least_min_budget(sc));
    assert(refill_all_within_period(sc));
    assert(refill_sum_to_budget(sc));
}

#define REFILL_SANITY_START(sc) ticks_t _sum = refill_sum(sc); sched_invariants(sc);
#define REFILL_SANITY_CHECK(sc, budget) \
    do { \
        assert(refill_sum(sc) == budget); sched_invariants(sc); \
    } while (0)

#define REFILL_SANITY_END(sc) \
    do {\
        REFILL_SANITY_CHECK(sc, _sum);\
    } while (0)
#else
#define REFILL_SANITY_START(sc)
#define REFILL_SANITY_CHECK(sc, budget)
#define REFILL_SANITY_END(sc)
#endif /* CONFIG_DEBUG_BUILD */

/* compute the sum of a refill queue */
static UNUSED ticks_t refill_sum(sched_context_t *sc)
{
    ticks_t sum = 0;
    word_t current = sc->scRefillHead;
    word_t seen = 0;

    while (seen < sc->scRefillCount) {
        sum += REFILL_INDEX(sc, current).rAmount;
        current = refill_next(sc, current);
        seen += 1;
    }

    return sum;
}

/* pop head of refill queue */
static inline refill_t refill_pop_head(sched_context_t *sc)
{
    /* queues cannot be smaller than 1 */
    assert(refill_size(sc) > 0);

    UNUSED word_t prev_size = refill_size(sc);
    refill_t refill = REFILL_HEAD(sc);
    sc->scRefillHead = refill_next(sc, sc->scRefillHead);
    sc->scRefillCount -= 1;

    /* sanity */
    assert(prev_size == (refill_size(sc) + 1));
    assert(sc->scRefillHead < sc->scRefillMax);
    return refill;
}

/* add item to tail of refill queue */
static inline void refill_add_tail(sched_context_t *sc, refill_t refill)
{
    /* cannot add beyond queue size */
    assert(refill_size(sc) < sc->scRefillMax);

    sc->scRefillCount += 1;
    REFILL_TAIL(sc) = refill;
}

#ifdef ENABLE_SMP_SUPPORT
void refill_new(sched_context_t *sc, word_t max_refills, ticks_t budget, ticks_t period, word_t core)
#else
void refill_new(sched_context_t *sc, word_t max_refills, ticks_t budget, ticks_t period)
#endif
{
    sc->scPeriod = period;
    sc->scBudget = budget;
    sc->scRefillHead = 0;
    sc->scRefillCount = 1;
    sc->scRefillMax = max_refills;
    assert(budget > MIN_BUDGET);
    /* full budget available */
    REFILL_HEAD(sc).rAmount = budget;
    /* budget can be used from now */
    REFILL_HEAD(sc).rTime = NODE_STATE_ON_CORE(ksCurTime, core);
    REFILL_SANITY_CHECK(sc, budget);
}

static inline void schedule_used(sched_context_t *sc, refill_t new)
{
    if (refill_empty(sc)) {
        assert(new.rAmount >= MIN_BUDGET);
        refill_add_tail(sc, new);
    } else {
        /* The refills being disjoint allows for them to be merged with
         * the resulting refill being earlier. */
        assert(new.rTime >= REFILL_TAIL(sc).rTime + REFILL_TAIL(sc).rAmount);

        /* schedule the used amount */
        if (new.rAmount < MIN_BUDGET && !refill_full(sc) && REFILL_TAIL(sc).rAmount + new.rAmount >= 2 * MIN_BUDGET) {
            /* Split tail into two parts of at least MIN_BUDGET */
            ticks_t remainder = MIN_BUDGET - new.rAmount;
            new.rAmount += remainder;
            new.rTime -= remainder;
            REFILL_TAIL(sc).rAmount -= remainder;
            refill_add_tail(sc, new);
        } else if (new.rAmount < MIN_BUDGET || refill_full(sc)) {
            /* Merge with existing tail */
            REFILL_TAIL(sc).rTime = new.rTime - REFILL_TAIL(sc).rAmount;
            REFILL_TAIL(sc).rAmount += new.rAmount;
        } else {
            refill_add_tail(sc, new);
        }
    }

    assert(!refill_empty(sc));
}

void refill_update(sched_context_t *sc, ticks_t new_period, ticks_t new_budget, word_t new_max_refills)
{
    /* refill must be initialised in order to be updated - otherwise refill_new should be used */
    assert(sc->scRefillMax > 0);

    /* this is called on an active thread. We want to preserve the sliding window constraint -
     * so over new_period, new_budget should not be exceeded even temporarily */

    /* move the head refill to the start of the list - it's ok as we're going to truncate the
     * list to size 1 - and this way we can't be in an invalid list position once new_max_refills
     * is updated */
    REFILL_INDEX(sc, 0) = REFILL_HEAD(sc);
    sc->scRefillHead = 0;
    /* truncate refill list to size 1 */
    sc->scRefillCount = 1;
    /* update max refills */
    sc->scRefillMax = new_max_refills;
    /* update period */
    sc->scPeriod = new_period;
    /* update budget */
    sc->scBudget = new_budget;

    if (refill_ready(sc)) {
        REFILL_HEAD(sc).rTime = NODE_STATE_ON_CORE(ksCurTime, sc->scCore);
    }

    if (REFILL_HEAD(sc).rAmount >= new_budget) {
        /* if the heads budget exceeds the new budget just trim it */
        REFILL_HEAD(sc).rAmount = new_budget;
    } else {
        /* otherwise schedule the rest for the next period */
        ticks_t unused = new_budget - REFILL_HEAD(sc).rAmount;
        refill_t new = { .rAmount = unused,
                         .rTime = REFILL_HEAD(sc).rTime + new_period - unused,
                       };
        schedule_used(sc, new);
    }

    REFILL_SANITY_CHECK(sc, new_budget);
}

void refill_budget_check(ticks_t usage)
{
    sched_context_t *sc = NODE_STATE(ksCurSC);
    assert(!isRoundRobin(sc));
    REFILL_SANITY_START(sc);

    /* After refill_unblock_check, which is called on exit from the
     * kernel, the head refill will have started at the last kernel
     * entry. As such, the new refill from the used will begin one
     * period after that entry. */

    ticks_t last_entry = REFILL_HEAD(sc).rTime;

    refill_t used = (refill_t) {
        .rAmount = usage,
        .rTime = last_entry + sc->scPeriod,
    };

    /* After refill_unblock_check, using more than the head refill
     * indicates a bandwidth overrun. */

    if (unlikely(!refill_ready(sc) || REFILL_HEAD(sc).rAmount < usage)) {
        /* Budget overrun so empty the refill list entirely and schedule
         * a single refill of the full budget far enough in the future
         * to restore the bandwidth limitation. */
        sc->scRefillCount = 0;
        used.rTime += usage;
        used.rAmount = sc->scBudget;
    } else if (unlikely(usage == REFILL_HEAD(sc).rAmount)) {
        refill_pop_head(sc);
    } else {
        ticks_t remnant = REFILL_HEAD(sc).rAmount - usage;

        if (remnant >= MIN_BUDGET) {
            /* Leave the head refill with all that was leftover */
            REFILL_HEAD(sc).rAmount = remnant;
            REFILL_HEAD(sc).rTime += usage;
        } else {
            /* Merge the remaining time to the start of the following
             * refill */
            refill_pop_head(sc);
            if (refill_empty(sc)) {
                /* Used will become the new head */
                used.rTime -= remnant;
                used.rAmount += remnant;
            } else {
                REFILL_HEAD(sc).rTime -= remnant;
                REFILL_HEAD(sc).rAmount += remnant;
            }
        }
    }

    /* Schedule all of the used time as a single refill. */
    schedule_used(sc, used);

    REFILL_SANITY_END(sc);
}

void refill_unblock_check(sched_context_t *sc)
{
    if (isRoundRobin(sc)) {
        /* nothing to do */
        return;
    }

    /* advance earliest activation time to now */
    REFILL_SANITY_START(sc);
    if (refill_ready(sc)) {
        NODE_STATE(ksReprogram) = true;

        REFILL_HEAD(sc).rTime = NODE_STATE_ON_CORE(ksCurTime, sc->scCore) + getKernelWcetTicks();

        /* merge available replenishments */
        while (refill_size(sc) > 1) {
            ticks_t amount = REFILL_HEAD(sc).rAmount;
            ticks_t tail = REFILL_HEAD(sc).rTime + amount;
            if (REFILL_INDEX(sc, refill_next(sc, sc->scRefillHead)).rTime <= tail) {
                refill_pop_head(sc);
                REFILL_HEAD(sc).rAmount += amount;
                REFILL_HEAD(sc).rTime = NODE_STATE_ON_CORE(ksCurTime, sc->scCore) + getKernelWcetTicks();
            } else {
                break;
            }
        }

        assert(refill_ready(sc));
        assert(refill_sufficient(sc, 0));
    }
    REFILL_SANITY_END(sc);
}
