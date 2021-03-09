/*
 * Copyright 2020, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <sel4/simple_types.h>
#include <sel4/constants.h>
#include <sel4/sel4_arch/constants.h>
#include <sel4/benchmark_track_types.h>

/*
 * Log event implementation
 * ========================
 *
 * Each event is a sequence of words in the log buffer. The first word
 * of an event is the seL4_LogEvent which describes the length in words
 * of the event and the event type. There is also an additional
 * component to the seL4_LogEvent with use dependend on the event type.
 *
 * For an event of type t, there will be an event identifier
 * seL4_Log_TypeId(t), and a struct representing the event
 * seL4_Log_Type(t).
 *
 * The event can be logged from within the kernel using
 * seL4_Log(t)(event_args...). seL4_Log expressions are automatically
 * removed if logging is disabled.
 */

/* The event header word which appears at the start of every event */
typedef struct {
    /* Type of event logged */
    seL4_Word type: 8;
    /* The remaining 24 or 56 bits of the header word can be used for
     * the event itself */
#if seL4_WordSizeBits == 2
    seL4_Word data: 24;
#elif seL4_WordSizeBits == 3
    seL4_Word data: 56;
#else
#error "Unsupported word seL4_WordSizeBits"
#endif
} seL4_LogEvent;

/* The name of the type identifier for a given log event type */
#define seL4_Log_TypeId(event) seL4_Log_TypeId_ ## event

/* The name of the stuct type for a given log event type */
#define seL4_Log_Type(event) seL4_Log_Struct_ ## event

/* Cast a log event to a particular log type */
#define seL4_Log_Cast(event) (seL4_Log_Type(event) *)

/* Get the length of an event structure as a number of words */
#define seL4_Log_Length(event) (((sizeof(seL4_Log_Type(event)) - 1ul) >> seL4_WordSizeBits) + 1ul)

/* Maximum length of name that will be logged into the log buffer */
#define SEL4_LOG_NAME_LENGTH 64

/*
 * Definition of log events
 * ========================
 *
 * Each log event must have a type identifier in the enum below, a
 * struct describing the layout of the message (with the first field
 * being the seL4_LogEvent).
 *
 * The functions used to log the events within the kernel are defined in
 * seL4/include/log.h
 */

/* Event type identifiers */
enum {
    seL4_Log_TypeId(None),
    seL4_Log_TypeId(Entry),
    seL4_Log_TypeId(Exit),
    seL4_Log_TypeId(Block),
    seL4_Log_TypeId(Resume),
    seL4_Log_TypeId(Postpone),
    seL4_Log_TypeId(SwitchThread),
    seL4_Log_TypeId(SwitchSchedContext),
    seL4_Log_TypeId(Timestamp),
    seL4_Log_TypeId(Irq),
    seL4_Log_TypeId(Syscall),
    seL4_Log_TypeId(Invocation),
    seL4_Log_TypeId(ThreadName),
    seL4_NumLogTypeIds,
};

/* Log an empty event */
typedef struct {
    seL4_LogEvent header;
} seL4_Log_Type(None);

/* Entry into kernel */
typedef struct {
    /* Header data contains core ID */
    seL4_LogEvent header;
    /* Timestamp from cycle counter */
    seL4_Uint64 timestamp;
} seL4_Log_Type(Entry);

/* Exit from kernel */
typedef struct {
    /* Header data contains core ID */
    seL4_LogEvent header;
    /* Timestamp from cycle counter */
    seL4_Uint64 timestamp;
} seL4_Log_Type(Exit);

/* Manner in which a thread can block */
typedef enum {
    seL4_Log_Block_EndpointRecieve,
    seL4_Log_Block_EndpointSend,
    seL4_Log_Block_Reply,
    seL4_Log_Block_NotificationRecieve,
    seL4_Log_NumValidBlockEvents,
} seL4_Log_BlockEvent;

/* Block on a kernel object */
typedef struct {
    /* Header data indicates nature of block */
    seL4_LogEvent header;
    /* Object on which the thread blocked (physical address) */
    seL4_Word object;
} seL4_Log_Type(Block);

/* Resume a thread (including unblock) */
typedef struct {
    seL4_LogEvent header;
    /* Thread that was unblocked (physical address) */
    seL4_Word thread;
} seL4_Log_Type(Resume);

/* Postpone the current SC */
typedef struct {
    seL4_LogEvent header;
    /* Time to which the thread was postponed */
    seL4_Uint64 release;
} seL4_Log_Type(Postpone);

/* Switch to running a thread */
typedef struct {
    /* Header data contains core ID */
    seL4_LogEvent header;
    /* Thread that is now running (physical address) */
    seL4_Word thread;
} seL4_Log_Type(SwitchThread);

/* Switch to running a scheduling context */
typedef struct {
    /* Header data contains core ID */
    seL4_LogEvent header;
    /* scheduling context that is now running (physical address) */
    seL4_Word sched_context;
} seL4_Log_Type(SwitchSchedContext);

/* Switch to running a scheduling context */
typedef struct {
    /* Header data contains core ID */
    seL4_LogEvent header;
    /* Kernel time in microseconds */
    seL4_Uint64 microseconds;
    /* Kernel time in cycles */
    seL4_Uint64 cycles;
} seL4_Log_Type(Timestamp);

/* IRQ received by kernel */
typedef struct {
    /* Header data contains core ID */
    seL4_LogEvent header;
    /* IRQ number */
    seL4_Word irq;
} seL4_Log_Type(Irq);

/* Syscall into kernel */
typedef struct {
    /* Header data contains negated syscall ID */
    seL4_LogEvent header;
    /* Syscall ID */
    seL4_Word syscall;
} seL4_Log_Type(Syscall);

/* Syscall into kernel */
typedef struct {
    /* Header data contains invocation label */
    seL4_LogEvent header;
    /* Capability invoked */
    seL4_Word cptr;
} seL4_Log_Type(Invocation);

/* Syscall into kernel */
typedef struct {
    /* Header data length of name in bytes */
    seL4_LogEvent header;
    /* Thread that is now running (physical address) */
    seL4_Word thread;
    /* Thread name */
    char name[SEL4_LOG_NAME_LENGTH];
} seL4_Log_Type(ThreadName);

/*
 * Reading information from log events
 * ===================================
 *
 * These functions can be used at user-level to inspect events from the
 * log buffer.
 */

/* Get the length of an event type */
static inline seL4_Word seL4_LogType_length(seL4_Word type)
{
    static seL4_Word type_lengths[seL4_NumLogTypeIds] = {
        [seL4_Log_TypeId(None)] = seL4_Log_Length(None),
        [seL4_Log_TypeId(Entry)] = seL4_Log_Length(Entry),
        [seL4_Log_TypeId(Exit)] = seL4_Log_Length(Exit),
        [seL4_Log_TypeId(Block)] = seL4_Log_Length(Block),
        [seL4_Log_TypeId(Resume)] = seL4_Log_Length(Resume),
        [seL4_Log_TypeId(Postpone)] = seL4_Log_Length(Postpone),
        [seL4_Log_TypeId(SwitchThread)] = seL4_Log_Length(SwitchThread),
        [seL4_Log_TypeId(SwitchSchedContext)] = seL4_Log_Length(SwitchSchedContext),
        [seL4_Log_TypeId(Timestamp)] = seL4_Log_Length(Timestamp),
        [seL4_Log_TypeId(Irq)] = seL4_Log_Length(Irq),
        [seL4_Log_TypeId(Syscall)] = seL4_Log_Length(Syscall),
        [seL4_Log_TypeId(Invocation)] = seL4_Log_Length(Invocation),
        [seL4_Log_TypeId(ThreadName)] = seL4_Log_Length(ThreadName),
    };

    if (type >= seL4_NumLogTypeIds) {
        return 0;
    } else {
        return type_lengths[type];
    }
}

/* Get the type of an event */
static inline seL4_Word seL4_LogEvent_type(seL4_LogEvent *event)
{
    if (event == seL4_Null) {
        return seL4_Log_TypeId(None);
    } else {
        return event->type;
    }
}

/* Get the length of an event */
static inline seL4_Word seL4_LogEvent_length(seL4_LogEvent *event)
{
    if (event == seL4_Null) {
        return 0;
    } else {
        return seL4_LogType_length(seL4_LogEvent_type(event));
    }
}

/*
 * Managing user-level log buffer references
 * =========================================
 *
 * These functions are used to create references to the log buffer at
 * user-level and iterate through the events in the buffer.
 *
 * The log buffer is a shared array of seL4_Word in memory. The first
 * word tracks the number of words in the array used for events and the
 * second word is the index that the next event will be written to. All
 * subsequent words then form the series of events.
 */

/* A log buffer reference */
typedef struct {
    seL4_Word *volatile buffer;
    seL4_Word index;
    /* The kernel uses this to track the size of the memory region but
     * user-level uses this to track how much of the buffer was actually
     * used by the kernel. */
    seL4_Word size;
} seL4_LogBuffer;

/* Create a new log buffer reference */
static inline seL4_LogBuffer seL4_LogBuffer_new(void *buffer)
{
    return (seL4_LogBuffer) {
        .buffer = buffer,
        .index = 0,
        .size = 0,
    };
}

/* Set the size of the log buffer */
static inline void seL4_LogBuffer_setSize(seL4_LogBuffer *buffer, seL4_Word words)
{
    buffer->size = words;
}

/* Reset a log buffer for new logging */
static inline void seL4_LogBuffer_reset(seL4_LogBuffer *buffer)
{
    buffer->size = 0;
    buffer->index = 0;
}

/* Get an event at a particular index in the buffer */
static inline seL4_LogEvent *seL4_LogBuffer_event(seL4_LogBuffer buffer, seL4_Word index)
{
    return (seL4_LogEvent *)(&buffer.buffer[index]);
}

/* Get the next event in the log buffer. */
static inline seL4_LogEvent *seL4_LogBuffer_next(seL4_LogBuffer *buffer)
{
    if (buffer == seL4_Null || buffer->buffer == seL4_Null || buffer->index >= buffer->size) {
        return seL4_Null;
    } else {
        seL4_LogEvent *event = seL4_LogBuffer_event(*buffer, buffer->index);

        seL4_Word length = seL4_LogEvent_length(event);
        if (length == 0) {
            return seL4_Null;
        }

        buffer->index += length;
        return event;
    }
}