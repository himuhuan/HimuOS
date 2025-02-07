#include "iocbuf.h"
#include "interrupt.h"
#include "kernel/task/sync.h"
#include "krnldbg.h"

#define ICB_NEXT_POS(pos) ((pos + 1) % IO_CIRCULAR_BUFFER_SIZE)

static void IcbWait(struct KR_TASK_STRUCT **waiter) {
    KASSERT(waiter != NULL && *waiter == NULL);
    *waiter = KrGetRunningThreadPcb();
    KrBlockThread(KR_THREAD_STATE_BLOCKED);
}

static void IcbWakeup(struct KR_TASK_STRUCT **waiter) {
    KASSERT(waiter != NULL && *waiter != NULL);
    KrUnblockThread(*waiter);
    *waiter = NULL;
}

void IcbInit(struct IO_CIR_BUFFER *icb) {
    KrLockInit(&icb->Lock);
    icb->Producer = icb->Consumer = NULL;
    icb->Tail = icb->Head = 0;
}

void IcbPut(struct IO_CIR_BUFFER *icb, char ch) {
    KASSERT(GetIntrStatus() == INTR_STATUS_OFF);

    while (IcbFull(icb)) {
        KrLockAcquire(&icb->Lock);
        IcbWait(&icb->Producer);
        KrLockRelease(&icb->Lock);
    }

    icb->Buf[icb->Head] = ch;
    icb->Head           = ICB_NEXT_POS(icb->Head);

    if (icb->Consumer != NULL)
        IcbWakeup(&icb->Consumer);
}

char IcbGet(struct IO_CIR_BUFFER *icb) {
    KASSERT(GetIntrStatus() == INTR_STATUS_OFF);

    while (IcbEmpty(icb)) {
        KrLockAcquire(&icb->Lock);
        IcbWait(&icb->Consumer);
        KrLockRelease(&icb->Lock);
    }

    char data = icb->Buf[icb->Tail];
    icb->Tail = ICB_NEXT_POS(icb->Tail);

    if (icb->Producer != NULL)
        IcbWakeup(&icb->Producer);

    return data;
}

BOOL IcbFull(struct IO_CIR_BUFFER *icb) {
    KASSERT(GetIntrStatus() == INTR_STATUS_OFF);
    return ICB_NEXT_POS(icb->Head) == icb->Tail;
}

BOOL IcbEmpty(struct IO_CIR_BUFFER *icb) {
    KASSERT(GetIntrStatus() == INTR_STATUS_OFF);
    return icb->Head == icb->Tail;
}