/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <config.h>

#ifdef CONFIG_KERNEL_DEBUG_LOG_BUFFER
#include <sel4/log.h>
#include <basic_types.h>
#include <arch/benchmark.h>
#include <model/statedata.h>
#include <arch/model/smp.h>
#include <object/structures.h>
#ifdef CONFIG_KERNEL_IS_MCS
#include <kernel/sporadic.h>
#endif

/* The global logbuffer reference used by the kernel */
extern seL4_LogBuffer ksLogBuffer;
extern bool_t ksLogEnabled;

/* Reset the log buffer to start logging at the beginning */
static inline void logBuffer_reset(void)
{
    if (ksLogBuffer.buffer != NULL) {
        ksLogBuffer.index = 0;
        ksLogEnabled = true;
    }
}

/* Initialise the kernel log buffer with a new memory region */
static inline void logBuffer_init(seL4_Word *buffer, seL4_Word words)
{
    ksLogBuffer = seL4_LogBuffer_new(buffer);
    seL4_LogBuffer_setSize(&ksLogBuffer, words);
    logBuffer_reset();
}

/* Finalise the log buffer and ensure no further events will be written. */
static inline word_t logBuffer_finalize(void)
{
    ksLogEnabled = false;
    return ksLogBuffer.index;
}

/* Clear the log buffer if buffer matches the given address */
static inline void logBuffer_maybeClear(seL4_Word *base_addr)
{
    if (ksLogBuffer.buffer == base_addr) {
        logBuffer_reset();
        logBuffer_finalize();
        ksLogBuffer.buffer = NULL;
    }
}

/* Get a reference to an event of a given size to next be written to a log */
static inline seL4_LogEvent *logBuffer_reserveGeneric(seL4_Word type)
{
    word_t length = seL4_LogType_length(type);
    assert(length >= seL4_Log_Length(None));
    word_t remaining = ksLogBuffer.size - ksLogBuffer.index;
    if (ksLogEnabled && remaining >= length) {
        /* Get a reference to the event and initialise */
        seL4_LogEvent *event = seL4_LogBuffer_event(ksLogBuffer, ksLogBuffer.index);

        /* Event lengths are recorded as one less */
        event->type = type;

        /* Advance the log to the next location */
        ksLogBuffer.index += length;

        return event;
    } else {
        if (ksLogEnabled) {
            /* Insufficient space in log buffer so finalise */
            logBuffer_finalize();
        }
        return NULL;
    }
}

/* Reserve an event in the buffer for a specific event type */
#define logBuffer_reserve(event) \
    (seL4_Log_Cast(event)(logBuffer_reserveGeneric( \
        seL4_Log_TypeId(event) \
    )))\

/*
 * Definition of log events
 * ========================
 *
 * Each log even must have a log function declared below.
 *
 * The data structures used to describe the events within the log buffer
 * are defined in libsel4/include/sel4/log.h
 */

/* Get the function name used to log an event */
#define debugLog_Function(event) seL4_Log_Function_ ## event

/* Log a particular event to the kernel log buffer */
#define debugLog(event, ...) debugLog_Function(event)(__VA_ARGS__)

/* Log a particular event if a condition holds */
#define debugLogIf(event, cond, ...) if (cond) debugLog(event, __VA_ARGS__)

/* Log an empty event */
static inline void debugLog_Function(None)(void)
{
    logBuffer_reserve(None);
}

/* Log a kernel entry */
static inline void debugLog_Function(Entry)(void)
{
#ifdef CONFIG_KERNEL_DEBUG_LOG_ENTRIES
    seL4_Log_Type(Entry) *event = logBuffer_reserve(Entry);
    if (event != NULL) {
        event->header.data = CURRENT_CPU_INDEX();
        event->timestamp = timestamp();
    }
#endif
}

/* Log a kernel exit */
static inline void debugLog_Function(Exit)(void)
{
#ifdef CONFIG_KERNEL_DEBUG_LOG_ENTRIES
    seL4_Log_Type(Exit) *event = logBuffer_reserve(Exit);
    if (event != NULL) {
        event->header.data = CURRENT_CPU_INDEX();
        event->timestamp = timestamp();
    }
#endif
}

/* Log the current thread blocking */
static inline void debugLog_Function(Block)(void *object) {
    word_t thread_state = thread_state_get_tsType(NODE_STATE(ksCurThread)->tcbState);
    seL4_Log_BlockEvent block = seL4_Log_NumValidBlockEvents + thread_state;

    switch (thread_state) {
    case ThreadState_BlockedOnReceive:
        block = seL4_Log_Block_EndpointRecieve;
        break;
    case ThreadState_BlockedOnSend:
        block = seL4_Log_Block_EndpointSend;
        break;
    case ThreadState_BlockedOnReply:
        block = seL4_Log_Block_Reply;
        break;
    case ThreadState_BlockedOnNotification:
        block = seL4_Log_Block_NotificationRecieve;
        break;
    }

    seL4_Log_Type(Block) *event = logBuffer_reserve(Block);
    if (event != NULL) {
        event->header.data = block;
        event->object = addrFromPPtr(object);
    }
}

/* Log a thread being resumed */
static inline void debugLog_Function(Resume)(tcb_t *thread) {
    seL4_Log_Type(Resume) *event = logBuffer_reserve(Resume);
    if (event != NULL) {
        /* The TCB is halfway through the object allocation, and we want
         * the address of the TCB allocation */
        event->thread = addrFromPPtr(thread) & ~MASK(seL4_TCBBits);
    }
}

/* Log current SC being postponed */
static inline void debugLog_Function(Postpone)(void) {
#ifdef CONFIG_KERNEL_MCS
    seL4_Log_Type(Postpone) *event = logBuffer_reserve(Postpone);
    if (event != NULL) {
        event->release = ticksToUs(refill_head(NODE_STATE(ksCurSC))->rTime);
    }
#endif
}

/* Log switching thread on a core */
static inline void debugLog_Function(SwitchThread)(void) {
    seL4_Log_Type(SwitchThread) *event = logBuffer_reserve(SwitchThread);
    if (event != NULL) {
        event->header.data = CURRENT_CPU_INDEX();
        /* The TCB is halfway through the object allocation, and we want
         * the address of the TCB allocation */
        event->thread = addrFromPPtr(NODE_STATE(ksCurThread)) & ~MASK(seL4_TCBBits);
    }
}

/* Log switching scheduling context on a core */
static inline void debugLog_Function(SwitchSchedContext)(void) {
#ifdef CONFIG_KERNEL_MCS
    seL4_Log_Type(SwitchSchedContext) *event = logBuffer_reserve(SwitchSchedContext);
    if (event != NULL) {
        event->header.data = CURRENT_CPU_INDEX();
        /* The TCB is halfway through the object allocation, and we want
         * the address of the TCB allocation */
        event->sched_context = addrFromPPtr(NODE_STATE(ksCurSC));
    }
#endif
}

/* Log time changing on a core */
static inline void debugLog_Function(Timestamp)(void) {
#ifdef CONFIG_KERNEL_MCS
    seL4_Log_Type(Timestamp) *event = logBuffer_reserve(Timestamp);
    if (event != NULL) {
        event->header.data = CURRENT_CPU_INDEX();
        event->microseconds = ticksToUs(NODE_STATE(ksCurTime));
#ifdef CONFIG_KERNEL_DEBUG_LOG_ENTRIES
        event->cycles = timestamp();
#endif
    }
#endif
}

#else /* CONFIG_KERNEL_DEBUG_LOG_BUFFER */
/* With logging disabled, any logging functions become no-ops */
#define debugLog(...)
#define debugLogIf(...)
#define ksLogEnabled false
#endif /* !CONFIG_KERNEL_DEBUG_LOG_BUFFER */
