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
#include "structs/list.h"

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
    void (*EIP)(void);
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

    void (*EIP)(KR_THREAD_ROUTINE *routine, void *routineArgs);
    void (*Reserved)(void);
    KR_THREAD_ROUTINE *Routine;
    void              *RoutineArgs;
};

#define KR_TASK_NAME_LEN 16

struct KR_TASK_STRUCT {
    /* 内核栈指针（Kernel Stack Pointer）存储任务在内核态执行时的栈顶地址 */
    uint32_t *KrnlStack;

    /* 任务状态（Thread State）*/
    enum KR_THREAD_STATE State;

    /* 任务名称（Task Name） */
    char Name[KR_TASK_NAME_LEN];

    /* 调度优先级（Scheduling Priority）值越大优先级越高 */
    uint8_t Priority;

    /* 剩余时间片（Remaining Time Slice）记录任务在当前调度周期内剩余的可运行时间单位 */
    uint8_t RemainingTicks;

    /* 累计运行时间（Elapsed Ticks）统计任务自创建以来消耗的总时钟滴答数析 */
    uint32_t ElapsedTicks;

    /* 调度队列链节点（Scheduler Queue Link） */
    struct KR_LIST_ELEMENT SchedTag;

    /* 全局任务链表节点（Global Task List Link） */
    struct KR_LIST_ELEMENT GlobalTag;

    /* 页目录指针（Page Directory Pointer）*/
    uint32_t *PageDir;

    /* 栈溢出保护值（Stack Canary） */
    uint32_t StackCanary;
};

struct KR_TASK_STRUCT *KrCreateThread(const char *name, int priority, KR_THREAD_ROUTINE routineAddr, void *routineArgs);

struct KR_TASK_STRUCT *KrGetRunningThreadPcb(void);

void KrDefaultSchedule(void);

void InitThread(void);

#define PCB_CANARY_MAGIC 0x13140721

#endif // ^^ __HIMUOS__KERNEL_TASK_SCHED_H ^^
