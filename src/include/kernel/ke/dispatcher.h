/**
 * HimuOperatingSystem
 *
 * File: ke/dispatcher.h
 * Description:
 * Ke Layer - Dispatcher object infrastructure: common header, wait block,
 * and type definitions for the unified wait model.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <lib/common/linked_list.h>

// ─────────────────────────────────────────────────────────────
// Dispatcher object type enumeration
// ─────────────────────────────────────────────────────────────

typedef enum KDISPATCHER_OBJECT_TYPE
{
    DISPATCHER_TYPE_EVENT = 0,
} KDISPATCHER_OBJECT_TYPE;

// ─────────────────────────────────────────────────────────────
// Dispatcher object common header
// ─────────────────────────────────────────────────────────────

typedef struct KDISPATCHER_HEADER
{
    uint32_t Signature; // KDISPATCHER_SIGNATURE when initialized
    KDISPATCHER_OBJECT_TYPE Type;
    int32_t SignalState;
    LINKED_LIST_TAG WaitListHead;
} KDISPATCHER_HEADER;

// ─────────────────────────────────────────────────────────────
// Wait block — embedded in each KTHREAD for single-object wait
// ─────────────────────────────────────────────────────────────

typedef struct KWAIT_BLOCK
{
    struct KDISPATCHER_HEADER *Dispatcher; // Object being waited on (NULL for timeout-only)
    LINKED_LIST_TAG WaitListLink;          // Link in dispatcher object's wait list
    LINKED_LIST_TAG TimeoutLink;           // Link in global timeout queue
    uint64_t DeadlineNs;                   // Absolute timeout deadline (0 = no timeout)
    HO_STATUS CompletionStatus;            // EC_SUCCESS or EC_TIMEOUT
    BOOL Completed;                        // Prevents double completion
} KWAIT_BLOCK;

// ─────────────────────────────────────────────────────────────
// Wait constants
// ─────────────────────────────────────────────────────────────

#define KDISPATCHER_SIGNATURE 0x4B444953U // 'KDIS'
#define KE_WAIT_INFINITE 0xFFFFFFFFFFFFFFFFULL
