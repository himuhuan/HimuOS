/**
 * HimuOperatingSystem
 *
 * File: ke/kthread.h
 * Description:
 * Ke Layer - Kernel thread object definition and lifecycle API.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <lib/common/linked_list.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/pool.h>
#include <kernel/ke/irql.h>
#include <kernel/ke/dispatcher.h>
#include <kernel/ke/event.h>

// ─────────────────────────────────────────────────────────────
// Thread entry point
// ─────────────────────────────────────────────────────────────

typedef void (*KTHREAD_ENTRY)(void *arg);

// ─────────────────────────────────────────────────────────────
// Thread state machine
// ─────────────────────────────────────────────────────────────

typedef enum KTHREAD_STATE
{
    KTHREAD_STATE_NEW = 0,
    KTHREAD_STATE_READY,
    KTHREAD_STATE_RUNNING,
    KTHREAD_STATE_BLOCKED,
    KTHREAD_STATE_TERMINATED
} KTHREAD_STATE;

typedef enum KTHREAD_TERMINATION_MODE
{
    KTHREAD_TERMINATION_MODE_DETACHED = 0,
    KTHREAD_TERMINATION_MODE_JOINABLE
} KTHREAD_TERMINATION_MODE;

typedef enum KTHREAD_TERMINATION_CLAIM_STATE
{
    KTHREAD_TERMINATION_CLAIM_STATE_UNCLAIMED = 0,
    KTHREAD_TERMINATION_CLAIM_STATE_JOIN_IN_PROGRESS,
    KTHREAD_TERMINATION_CLAIM_STATE_CONSUMED
} KTHREAD_TERMINATION_CLAIM_STATE;

// ─────────────────────────────────────────────────────────────
// Context switch structure (callee-saved registers only)
// ─────────────────────────────────────────────────────────────

typedef struct KTHREAD_CONTEXT
{
    uint64_t RBX; // offset  0
    uint64_t RBP; // offset  8
    uint64_t R12; // offset 16
    uint64_t R13; // offset 24
    uint64_t R14; // offset 32
    uint64_t R15; // offset 40
    uint64_t RSP; // offset 48
    uint64_t RIP; // offset 56  (informational; actual RIP is on stack for KiSwitchContext)
} KTHREAD_CONTEXT;

// ─────────────────────────────────────────────────────────────
// KTHREAD structure
// ─────────────────────────────────────────────────────────────

#define KTHREAD_FLAG_IDLE (1U << 0)

typedef struct KTHREAD
{
    uint32_t ThreadId;
    KTHREAD_STATE State;
    KTHREAD_CONTEXT Context;

    uint64_t StackBase;      // Lowest usable virtual address of the thread stack
    uint64_t StackSize;      // Usable stack size in bytes
    uint64_t StackGuardBase; // Lowest guard-page virtual address (0 if not KVA-managed)
    BOOL StackOwnedByKva;
    KE_KVA_RANGE StackRange;

    uint8_t Priority; // Reserved for multi-priority extension
    uint64_t Quantum; // Time slice remaining (nanoseconds)
    uint32_t OwnedMutexCount;
    KE_IRQL_STATE IrqlState;

    KWAIT_BLOCK WaitBlock; // Embedded wait record for unified wait model
    KEVENT TerminationCompletion;
    KTHREAD_TERMINATION_MODE TerminationMode;
    KTHREAD_TERMINATION_CLAIM_STATE TerminationClaimState;

    LINKED_LIST_TAG ReadyLink; // Ready queue / terminated list intrusive node

    KTHREAD_ENTRY EntryPoint;
    void *EntryArg;
    uint32_t Flags;
} KTHREAD;

// ─────────────────────────────────────────────────────────────
// KTHREAD pool API
// ─────────────────────────────────────────────────────────────

extern KE_POOL gKThreadPool;

/**
 * @brief Initialize the global KTHREAD object pool.
 *
 * Call after KeKvaInit() has brought up the KVA heap foundation, because this
 * routine delegates to KePoolInit() and the pool now grows through
 * KeHeapAllocPages().
 */
HO_KERNEL_API HO_STATUS KeKThreadPoolInit(void);

// ─────────────────────────────────────────────────────────────
// Assembly context switch primitive (defined in context_switch.asm)
// ─────────────────────────────────────────────────────────────

extern void KiSwitchContext(KTHREAD_CONTEXT *prev, KTHREAD_CONTEXT *next);
