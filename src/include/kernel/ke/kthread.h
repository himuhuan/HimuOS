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
#include <kernel/ke/pool.h>
#include <kernel/ke/dispatcher.h>

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

    uint64_t StackBase;            // Virtual address (HHDM) of stack base (lowest addr)
    uint64_t StackSize;            // Stack size in bytes
    HO_PHYSICAL_ADDRESS StackPhys; // Physical address of stack pages (0 if boot stack)

    uint8_t Priority; // Reserved for multi-priority extension
    uint64_t Quantum; // Time slice remaining (nanoseconds)
    uint32_t OwnedMutexCount;

    KWAIT_BLOCK WaitBlock; // Embedded wait record for unified wait model

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
 */
HO_KERNEL_API HO_STATUS KeKThreadPoolInit(void);

// ─────────────────────────────────────────────────────────────
// Assembly context switch primitive (defined in context_switch.asm)
// ─────────────────────────────────────────────────────────────

extern void KiSwitchContext(KTHREAD_CONTEXT *prev, KTHREAD_CONTEXT *next);
