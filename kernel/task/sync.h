/**
 * HIMU OPERATING SYSTEM
 *
 * File: sync.h
 * Synchronization, semaphore and lock
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS__KERNEL_TASK_SYNC_H
#define __HIMUOS__KERNEL_TASK_SYNC_H

#include <stdint.h>
#include "structs/list.h"
#include "sched.h"
#include "krnltypes.h"

struct KR_BINARY_SEMAPHONE {
    BOOL           Value;
    struct KR_LIST Waiters;
};

void KrBinSemaInit(struct KR_BINARY_SEMAPHONE *semaphone, BOOL initVal);

void KrBinSemaAcquire(struct KR_BINARY_SEMAPHONE *semaphone);

void KrBinSemaRelease(struct KR_BINARY_SEMAPHONE *semaphone);

struct KR_LOCK {
    struct KR_TASK_STRUCT     *Holder;
    struct KR_BINARY_SEMAPHONE Semaphone;
    uint32_t                   HolderRepeatedCnt;
};

void KrLockInit(struct KR_LOCK *lock);

void KrLockAcquire(struct KR_LOCK *lock);

void KrLockRelease(struct KR_LOCK *lock);

#endif //! __HIMUOS__KERNEL_TASK_SYNC_H