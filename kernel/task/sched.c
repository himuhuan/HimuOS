/**
 * HIMU OPERATING SYSTEM
 *
 * File: sched.c
 * Kernel schedule: task, thread, process
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */
#include <string.h>

#include "sched.h"
#include "kernel/memory.h"
#include "structs/list.h"
#include "interrupt.h"
#include "krnldbg.h"
#include "krnlio.h"

struct KR_TASK_STRUCT         *gKrnlMainThread;
struct KR_LIST                 gReadyThreadList;
struct KR_LIST                 gAllThreadList;
static struct KR_LIST_ELEMENT *kCurrentTag;

extern void KrSwitchTo(struct KR_TASK_STRUCT *curr, struct KR_TASK_STRUCT *next);

static void KernelThreadCallback(KR_THREAD_ROUTINE routine, void *routineArgs) {
    EnableIntr();
    routine(routineArgs);
}

static void InitThreadInfo(struct KR_TASK_STRUCT *pcb, const char *name, int priority) {
    strcpy(pcb->Name, name);

    pcb->State          = (pcb == gKrnlMainThread) ? KR_THREAD_STATE_RUNNING : KR_THREAD_STATE_READY;
    pcb->Priority       = priority;
    pcb->RemainingTicks = priority;
    pcb->ElapsedTicks   = 0;
    pcb->PageDir        = NULL;
    pcb->KrnlStack      = (uint32_t *)((uint32_t)pcb + MEM_PAGE_SIZE);
    pcb->StackCanary    = PCB_CANARY_MAGIC;
}

static void BuildKernelMainThread(void) {
    gKrnlMainThread = KrGetRunningThreadPcb();
    InitThreadInfo(gKrnlMainThread, "kernel", 31);
    KASSERT(!KrListHasElement(&gAllThreadList, &gKrnlMainThread->GlobalTag));
    KrInsertListTail(&gAllThreadList, &gKrnlMainThread->GlobalTag);
}

///////////////////////////////////////////////////////////////////////////////
// Public APIs
///////////////////////////////////////////////////////////////////////////////

struct KR_TASK_STRUCT *KrGetRunningThreadPcb(void) {
    uint32_t esp;
    asm("mov %%esp, %0" : "=g"(esp));
    return (struct KR_TASK_STRUCT *)(esp & 0xFFFFF000);
}

struct KR_TASK_STRUCT *
KrCreateThread(const char *name, int priority, KR_THREAD_ROUTINE routineAddr, void *routineArgs) {
    struct KR_TASK_STRUCT *pcb;

    if ((pcb = KrAllocKernelMemPage(1)) == NULL)
        return NULL;
    InitThreadInfo(pcb, name, priority);

    pcb->KrnlStack -= sizeof(struct KR_INTR_STACK);
    pcb->KrnlStack -= sizeof(struct KR_THREAD_STACK);

    /* init. thread stack */
    struct KR_THREAD_STACK *threadStack = (struct KR_THREAD_STACK *)pcb->KrnlStack;

    threadStack->EIP         = KernelThreadCallback;
    threadStack->Routine     = routineAddr;
    threadStack->RoutineArgs = routineArgs;
    threadStack->EBP = threadStack->EBX = threadStack->EDI = threadStack->ESI = 0;

    KASSERT(!KrListHasElement(&gReadyThreadList, &pcb->SchedTag));
    KrInsertListTail(&gReadyThreadList, &pcb->SchedTag);

    KASSERT(!KrListHasElement(&gAllThreadList, &pcb->GlobalTag));
    KrInsertListTail(&gAllThreadList, &pcb->GlobalTag);

    return pcb;
}

void KrDefaultSchedule(void) {
    struct KR_TASK_STRUCT *curr, *next;

    KASSERT(GetIntrStatus() == INTR_STATUS_OFF);

    curr = KrGetRunningThreadPcb();
    if (curr->State == KR_THREAD_STATE_RUNNING) {
        KASSERT(!KrListHasElement(&gReadyThreadList, &curr->SchedTag));
        KrInsertListTail(&gReadyThreadList, &curr->SchedTag);
        curr->RemainingTicks = curr->Priority;
        curr->State          = KR_THREAD_STATE_READY;
    }

    KASSERT(!KrListIsEmpty(&gReadyThreadList));
    kCurrentTag = KrListPopHeader(&gReadyThreadList);
    next        = CONTAINER_OF(kCurrentTag, struct KR_TASK_STRUCT, SchedTag);
    next->State = KR_THREAD_STATE_RUNNING;
    KrSwitchTo(curr, next);
}

void InitThread(void) {
    PrintStr("InitThread START\n");
    KrInitializeList(&gReadyThreadList);
    KrInitializeList(&gAllThreadList);
    BuildKernelMainThread();
    PrintStr("InitThread DONE\n");
}