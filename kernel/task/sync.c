/**
 * HIMU OPERATING SYSTEM
 *
 * File: sync.c
 * Synchronization, semaphore and lock
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "sync.h"
#include "interrupt.h"
#include "krnldbg.h"

void KrBinSemaInit(struct KR_BINARY_SEMAPHONE *semaphone, BOOL initVal) {
    KASSERT(initVal == 0 || initVal == 1);
    semaphone->Value = initVal;
    KrInitializeList(&semaphone->Waiters);
}

void KrBinSemaAcquire(struct KR_BINARY_SEMAPHONE *semaphone) {
    uint8_t                intr;
    struct KR_TASK_STRUCT *pcb;

    intr = DisableIntr();
    pcb  = KrGetRunningThreadPcb();
    while (!semaphone->Value) {
        if (KrListHasElement(&semaphone->Waiters, &pcb->SchedTag))
            KPANIC("KrSemaAcquire: blocked thread repeated in semaphone Waiters list");
        KrInsertListTail(&semaphone->Waiters, &pcb->SchedTag);
        KrBlockThread(KR_THREAD_STATE_BLOCKED);
    }

    // acquired semaphone
    semaphone->Value--;
    KASSERT(!semaphone->Value);

    SetIntrStatus(intr);
}

void KrBinSemaRelease(struct KR_BINARY_SEMAPHONE *semaphone) {
    uint8_t intr;
    intr = DisableIntr();
    KASSERT(semaphone->Value == 0);
    semaphone->Value++;
    if (!KrListIsEmpty(&semaphone->Waiters)) {
        struct KR_LIST_ELEMENT *element = KrListPopHeader(&semaphone->Waiters);
        struct KR_TASK_STRUCT  *task    = CONTAINER_OF(element, struct KR_TASK_STRUCT, SchedTag);
        KrUnblockThread(task);
    }
    KASSERT(semaphone->Value == 1);
    SetIntrStatus(intr);
}

void KrLockInit(struct KR_LOCK *lock) {
    lock->Holder = NULL;
    KrBinSemaInit(&lock->Semaphone, TRUE);
    lock->HolderRepeatedCnt = 0;
}

void KrLockAcquire(struct KR_LOCK *lock) {
    struct KR_TASK_STRUCT *currThread = KrGetRunningThreadPcb();
    if (lock->Holder != currThread) {
        KrBinSemaAcquire(&lock->Semaphone);
        KASSERT(lock->HolderRepeatedCnt == 0);
        lock->HolderRepeatedCnt = 1;
        lock->Holder            = currThread;
    } else {
        lock->HolderRepeatedCnt++;
    }
}

void KrLockRelease(struct KR_LOCK *lock) {
    KASSERT(lock->Holder == KrGetRunningThreadPcb());
    if (lock->HolderRepeatedCnt > 1) {
        lock->HolderRepeatedCnt--;
        return;
    }
    KASSERT(lock->HolderRepeatedCnt == 1);
    lock->Holder            = NULL;
    lock->HolderRepeatedCnt = 0;
    KrBinSemaRelease(&lock->Semaphone);
}