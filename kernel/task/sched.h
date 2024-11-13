/**
 * HIMU OPERATING SYSTEM
 *
 * File: sched.h
 * Kernel schedule: task, thread, process
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS__KERNEL_TASK_SCHED_H
#define __HIMUOS__KERNEL_TASK_SCHED_H

#include <stddef.h>

typedef void KR_THREAD_ROUTINE(void *);

enum KR_THREAD_STATE {
    KR_THREAD_STATE_RUNNING = 0,
    KR_THREAD_STATE_READY,
    KR_THREAD_STATE_BLOCKED,
    KR_THREAD_STATE_WAITING,
    KR_THREAD_STATE_HANGING,
    KR_THREAD_STATE_DIED
};

struct KR_INTR_STACK {
    uint32_t IntrId;
    uint32_t EDI;
    uint32_t ESI;
    uint32_t EBP;
    uint32_t ESPDummy;
    uint32_t EBX;
    uint32_t EDX;
    uint32_t ECX;
    uint32_t EAX;
    uint32_t GS;
    uint32_t FS;
    uint32_t ES;
    uint32_t DS;

    uint32_t ErrorCode;
    void     (*EIP)(void);
    uint32_t CS;
    uint32_t EFlags;
    void    *ESP;
    uint32_t SS;
};

struct KR_THREAD_STACK {
    uint32_t EBP;
    uint32_t EBX;
    uint32_t EDI;
    uint32_t ESI;

    void               (*EIP)(KR_THREAD_ROUTINE *routine, void *routineArgs);
    void               (*Reserved)(void);
    KR_THREAD_ROUTINE *Routine;
    void              *RoutineArgs;
};

#define KR_TASK_NAME_LEN 16

struct KR_TASK_STRUCT {
    uint32_t *KrnlStack;
    uint8_t   State;
    uint8_t   Priority;
    char      Name[KR_TASK_NAME_LEN];
    uint32_t  StackCanary;
};

struct KR_TASK_STRUCT *KrCreateThread(const char *name, int priority, KR_THREAD_ROUTINE routineAddr, void *routineArgs);

#endif // ^^ __HIMUOS__KERNEL_TASK_SCHED_H ^^
