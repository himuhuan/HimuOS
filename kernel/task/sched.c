/**
 * HIMU OPERATING SYSTEM
 *
 * File: sched.c
 * Kernel schedule: task, thread, process
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */
#include "sched.h"
#include "kernel/memory.h"
#include <string.h>

#define PCB_CANARY_MAGIC 0x13140721

static void KernelThreadCallback(KR_THREAD_ROUTINE routine, void *routineArgs) { routine(routineArgs); }

struct KR_TASK_STRUCT *
KrCreateThread(const char *name, int priority, KR_THREAD_ROUTINE routineAddr, void *routineArgs) {
    struct KR_TASK_STRUCT *pcb;

    if ((pcb = KrAllocKernelMemPage(1)) == NULL)
        return NULL;
    strcpy(pcb->Name, name);
    pcb->State       = KR_THREAD_STATE_RUNNING;
    pcb->Priority    = priority;
    pcb->KrnlStack   = (uint32_t *)((uint32_t)pcb + MEM_PAGE_SIZE);
    pcb->StackCanary = PCB_CANARY_MAGIC;

    pcb->KrnlStack -= sizeof(struct KR_INTR_STACK);
    pcb->KrnlStack -= sizeof(struct KR_THREAD_STACK);

    /* init. thread stack */
    struct KR_THREAD_STACK *threadStack = (struct KR_THREAD_STACK *)pcb->KrnlStack;

    threadStack->EIP         = KernelThreadCallback;
    threadStack->Routine     = routineAddr;
    threadStack->RoutineArgs = routineArgs;
    threadStack->EBP = threadStack->EBX = threadStack->EDI = threadStack->ESI = 0;

    asm volatile("movl %0, %%esp; \
    	pop %%ebp; pop %%ebx; pop %%edi; pop %%esi; \
    	ret"
                 :
                 : "g"(pcb->KrnlStack)
                 : "memory");

    return pcb;
}