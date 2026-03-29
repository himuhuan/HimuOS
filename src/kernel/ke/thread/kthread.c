/**
 * HimuOperatingSystem
 *
 * File: ke/thread/kthread.c
 * Description:
 * Ke Layer - KTHREAD object pool and lifecycle management.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/kthread.h>
#include <kernel/ke/irql.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/mm.h>
#include <kernel/hodefs.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

// ─────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────

#define MAX_KTHREADS 128

KE_POOL gKThreadPool;
static uint32_t gNextThreadId = 1;

// ─────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────

extern void KiThreadTrampoline(void);

// ─────────────────────────────────────────────────────────────
// Pool init
// ─────────────────────────────────────────────────────────────

HO_KERNEL_API HO_STATUS
KeKThreadPoolInit(void)
{
    return KePoolInit(&gKThreadPool, sizeof(KTHREAD), MAX_KTHREADS, "KTHREAD");
}

HO_KERNEL_API HO_STATUS
KeThreadCreate(KTHREAD **outThread, KTHREAD_ENTRY entryPoint, void *arg)
{
    if (!outThread || !entryPoint)
        return EC_ILLEGAL_ARGUMENT;

    KTHREAD *thread = (KTHREAD *)KePoolAlloc(&gKThreadPool);
    if (!thread)
        return EC_OUT_OF_RESOURCE;

    KE_KVA_RANGE stackRange;
    HO_STATUS status = KeKvaAllocRange(KE_KVA_ARENA_STACK, KE_THREAD_STACK_PAGES, 1, 0, TRUE, &stackRange);
    if (status != EC_SUCCESS)
    {
        KePoolFree(&gKThreadPool, thread);
        return status;
    }

    status = KeKvaMapOwnedPages(&stackRange, PTE_WRITABLE | PTE_GLOBAL | PTE_NO_EXECUTE);
    if (status != EC_SUCCESS)
    {
        KePoolFree(&gKThreadPool, thread);
        return status;
    }

    uint64_t stackTop = stackRange.UsableBase + KE_THREAD_STACK_SIZE;

    // Set up initial stack for KiSwitchContext's RET
    // After RET: RSP = stackTop - 8 (ABI: 16n+8 at function entry)
    uint64_t *sp = (uint64_t *)stackTop;
    sp--;
    *sp = 0; // dummy return address (alignment / KiThreadTrampoline never returns)
    sp--;
    *sp = (uint64_t)KiThreadTrampoline; // RET target for KiSwitchContext

    // Initialize KTHREAD
    thread->ThreadId = gNextThreadId++;
    thread->State = KTHREAD_STATE_NEW;

    memset(&thread->Context, 0, sizeof(KTHREAD_CONTEXT));
    thread->Context.RSP = (uint64_t)sp;
    thread->Context.RIP = (uint64_t)KiThreadTrampoline; // informational

    thread->StackBase = stackRange.UsableBase;
    thread->StackSize = KE_THREAD_STACK_SIZE;
    thread->StackGuardBase = stackRange.BaseAddress;
    thread->StackOwnedByKva = TRUE;

    thread->Priority = 0;
    thread->Quantum = KE_DEFAULT_QUANTUM_NS;
    thread->OwnedMutexCount = 0;
    KeInitializeIrqlState(&thread->IrqlState);

    // Initialize embedded wait block
    thread->WaitBlock.Dispatcher = NULL;
    LinkedListInit(&thread->WaitBlock.WaitListLink);
    LinkedListInit(&thread->WaitBlock.TimeoutLink);
    thread->WaitBlock.DeadlineNs = 0;
    thread->WaitBlock.CompletionStatus = EC_SUCCESS;
    thread->WaitBlock.Completed = FALSE;

    LinkedListInit(&thread->ReadyLink);

    thread->EntryPoint = entryPoint;
    thread->EntryArg = arg;
    thread->Flags = 0;

    *outThread = thread;

    klog(KLOG_LEVEL_INFO, "[SCHED] Thread %u created (entry=%p stack=%p guard=%p)\n", thread->ThreadId,
         (void *)entryPoint, (void *)thread->StackBase, (void *)thread->StackGuardBase);
    return EC_SUCCESS;
}
