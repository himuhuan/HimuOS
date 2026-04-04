/**
 * HimuOperatingSystem
 *
 * File: ke/user_bootstrap_syscall.c
 * Description: Minimal bootstrap raw syscall dispatcher and user-copy helpers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <arch/amd64/idt.h>
#include <arch/amd64/pm.h>
#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/hodbg.h>
#include <kernel/ke/console.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_bootstrap.h>
#include <libc/string.h>

static BOOL gKeUserBootstrapRawSyscallInitialized;

static int64_t KiEncodeRawSyscallStatus(HO_STATUS status);
static HO_NORETURN void KiAbortRawExit(KTHREAD *thread,
                                       uint64_t exitCode,
                                       HO_STATUS status,
                                       const char *reason);
static HO_STATUS KiValidateBootstrapUserRange(const KE_USER_BOOTSTRAP_LAYOUT *layout,
                                              HO_VIRTUAL_ADDRESS userBase,
                                              uint64_t length,
                                              HO_VIRTUAL_ADDRESS *outEndExclusive);
static HO_STATUS KiValidateBootstrapUserReadablePages(const KE_KERNEL_ADDRESS_SPACE *space,
                                                      HO_VIRTUAL_ADDRESS userBase,
                                                      HO_VIRTUAL_ADDRESS endExclusive);
static HO_STATUS KiCopyInBootstrapUserBytes(void *kernelDestination, HO_VIRTUAL_ADDRESS userSource, uint64_t length);
static int64_t KiRejectRawWrite(uint64_t userBuffer, uint64_t length, HO_STATUS status);
static int64_t KiHandleRawWrite(uint64_t userBuffer, uint64_t length);
static int64_t KiHandleRawExit(uint64_t exitCode);
static int64_t KiDispatchRawSyscall(uint64_t rawSyscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2);
static void KiHandleRawSyscallTrap(void *frame, void *context);

static int64_t
KiEncodeRawSyscallStatus(HO_STATUS status)
{
    return status == EC_SUCCESS ? 0 : -(int64_t)status;
}

static HO_NORETURN void
KiAbortRawExit(KTHREAD *thread, uint64_t exitCode, HO_STATUS status, const char *reason)
{
    klog(KLOG_LEVEL_ERROR,
         KE_USER_BOOTSTRAP_LOG_TEARDOWN_FAILED " raw=exit unrecoverable thread=%u code=%lu reason=%s status=%s (%d)\n",
         thread ? thread->ThreadId : 0U,
         (unsigned long)exitCode,
         reason,
         KrGetStatusMessage(status),
         status);
    HO_KPANIC(status, reason);
}

static HO_STATUS
KiValidateBootstrapUserRange(const KE_USER_BOOTSTRAP_LAYOUT *layout,
                             HO_VIRTUAL_ADDRESS userBase,
                             uint64_t length,
                             HO_VIRTUAL_ADDRESS *outEndExclusive)
{
    if (!layout || !outEndExclusive)
        return EC_ILLEGAL_ARGUMENT;

    if (layout->UserRangeEndExclusive <= layout->UserRangeBase)
        return EC_INVALID_STATE;

    if (length == 0)
    {
        *outEndExclusive = userBase;
        return EC_SUCCESS;
    }

    if (userBase < layout->UserRangeBase)
        return EC_ILLEGAL_ARGUMENT;

    if (userBase >= layout->UserRangeEndExclusive)
        return EC_ILLEGAL_ARGUMENT;

    HO_VIRTUAL_ADDRESS endExclusive = userBase + length;
    if (endExclusive < userBase)
        return EC_ILLEGAL_ARGUMENT;

    if (endExclusive > layout->UserRangeEndExclusive)
        return EC_ILLEGAL_ARGUMENT;

    *outEndExclusive = endExclusive;
    return EC_SUCCESS;
}

static HO_STATUS
KiValidateBootstrapUserReadablePages(const KE_KERNEL_ADDRESS_SPACE *space,
                                     HO_VIRTUAL_ADDRESS userBase,
                                     HO_VIRTUAL_ADDRESS endExclusive)
{
    if (!space || !space->Initialized)
        return EC_INVALID_STATE;

    if (userBase == endExclusive)
        return EC_SUCCESS;

    HO_VIRTUAL_ADDRESS pageBase = HO_ALIGN_DOWN(userBase, PAGE_4KB);
    HO_VIRTUAL_ADDRESS lastPageBase = HO_ALIGN_DOWN(endExclusive - 1, PAGE_4KB);

    for (;;)
    {
        KE_PT_MAPPING mapping = {0};
        HO_STATUS status = KePtQueryPage(space, pageBase, &mapping);
        if (status != EC_SUCCESS)
        {
            klog(KLOG_LEVEL_WARNING,
                 KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER " query failed addr=%p status=%s (%d)\n",
                 (void *)(uint64_t)pageBase,
                 KrGetStatusMessage(status),
                 status);
            return status;
        }

        if (!mapping.Present || !mapping.UserAccessible)
        {
            klog(KLOG_LEVEL_WARNING,
                 KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER " addr=%p present=%u userReachable=%u attrs=%p\n",
                 (void *)(uint64_t)pageBase,
                 mapping.Present,
                 mapping.UserAccessible,
                 (void *)(uint64_t)mapping.Attributes);
            return EC_ILLEGAL_ARGUMENT;
        }

        if (pageBase == lastPageBase)
            break;

        pageBase += PAGE_4KB;
    }

    return EC_SUCCESS;
}

static HO_STATUS
KiCopyInBootstrapUserBytes(void *kernelDestination, HO_VIRTUAL_ADDRESS userSource, uint64_t length)
{
    if (!kernelDestination)
        return EC_ILLEGAL_ARGUMENT;

    if (length == 0)
        return EC_SUCCESS;

    KE_USER_BOOTSTRAP_LAYOUT layout = {0};
    HO_STATUS status = KeUserBootstrapQueryCurrentThreadLayout(&layout);
    if (status != EC_SUCCESS)
        return status;

    HO_VIRTUAL_ADDRESS endExclusive = 0;
    status = KiValidateBootstrapUserRange(&layout, userSource, length, &endExclusive);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING,
             KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER " range addr=%p len=%lu status=%s (%d)\n",
             (void *)(uint64_t)userSource,
             (unsigned long)length,
             KrGetStatusMessage(status),
             status);
        return status;
    }

    HO_PHYSICAL_ADDRESS activeRoot = 0;
    status = KeQueryActiveRootPageTable(&activeRoot);
    if (status != EC_SUCCESS)
        return status;

    if (activeRoot != layout.OwnerRootPageTablePhys)
    {
        klog(KLOG_LEVEL_WARNING,
             KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER " active-root=%p owner-root=%p addr=%p len=%lu\n",
             (void *)(uint64_t)activeRoot,
             (void *)(uint64_t)layout.OwnerRootPageTablePhys,
             (void *)(uint64_t)userSource,
             (unsigned long)length);
        return EC_INVALID_STATE;
    }

    KE_KERNEL_ADDRESS_SPACE ownerView = {0};
    ownerView.RootPageTablePhys = layout.OwnerRootPageTablePhys;
    ownerView.Initialized = TRUE;

    status = KiValidateBootstrapUserReadablePages(&ownerView, userSource, endExclusive);
    if (status != EC_SUCCESS)
        return status;

    memcpy(kernelDestination, (const void *)(uint64_t)userSource, (size_t)length);
    return EC_SUCCESS;
}

static int64_t
KiRejectRawWrite(uint64_t userBuffer, uint64_t length, HO_STATUS status)
{
    klog(KLOG_LEVEL_WARNING,
         KE_USER_BOOTSTRAP_LOG_INVALID_RAW_WRITE " addr=%p len=%lu status=%s (%d)\n",
         (void *)(uint64_t)userBuffer,
         (unsigned long)length,
         KrGetStatusMessage(status),
         status);
    return KiEncodeRawSyscallStatus(status);
}

static int64_t
KiHandleRawWrite(uint64_t userBuffer, uint64_t length)
{
    if (length > KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH)
    {
        klog(KLOG_LEVEL_WARNING,
             KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER " raw=write addr=%p len=%lu exceeds=%u\n",
             (void *)(uint64_t)userBuffer,
             (unsigned long)length,
             KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH);
        return KiRejectRawWrite(userBuffer, length, EC_ILLEGAL_ARGUMENT);
    }

    if (length == 0)
        return 0;

    char scratch[KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH];
    HO_STATUS status = KiCopyInBootstrapUserBytes(scratch, (HO_VIRTUAL_ADDRESS)userBuffer, length);
    if (status != EC_SUCCESS)
        return KiRejectRawWrite(userBuffer, length, status);

    uint64_t written = 0;
    KTHREAD *thread = KeGetCurrentThread();
    for (uint64_t index = 0; index < length; ++index)
    {
        if (ConsoleWriteChar(scratch[index]) < 0)
        {
            klog(KLOG_LEVEL_ERROR,
                 "[USERBOOT] SYS_RAW_WRITE console emit failed index=%lu thread=%u\n",
                 (unsigned long)index,
                 thread ? thread->ThreadId : 0U);
            return KiEncodeRawSyscallStatus(EC_FAILURE);
        }

        ++written;
    }

    klog(KLOG_LEVEL_INFO,
         KE_USER_BOOTSTRAP_LOG_HELLO_WRITE_SUCCEEDED " bytes=%lu thread=%u\n",
         (unsigned long)written,
         thread ? thread->ThreadId : 0U);

    return (int64_t)written;
}

static int64_t
KiHandleRawExit(uint64_t exitCode)
{
    KTHREAD *thread = KeGetCurrentThread();
    if (!thread || ExBootstrapAdapterQueryThreadStaging(thread) == NULL)
        KiAbortRawExit(thread, exitCode, EC_INVALID_STATE, "Bootstrap raw exit missing staging");

    klog(KLOG_LEVEL_INFO,
         KE_USER_BOOTSTRAP_LOG_SYS_RAW_EXIT " code=%lu thread=%u\n",
         (unsigned long)exitCode,
         thread->ThreadId);

    HO_STATUS status = ExBootstrapAdapterHandleRawExit(thread);
    if (status != EC_SUCCESS)
        KiAbortRawExit(thread, exitCode, status, "Bootstrap raw exit teardown failed after no-return transition");

    klog(KLOG_LEVEL_INFO,
         KE_USER_BOOTSTRAP_LOG_TEARDOWN_COMPLETE " code=%lu thread=%u\n",
         (unsigned long)exitCode,
         thread->ThreadId);

    KeThreadExit();
}

static int64_t
KiDispatchRawSyscall(uint64_t rawSyscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    KTHREAD *thread = KeGetCurrentThread();
    if (!thread || ExBootstrapAdapterQueryThreadStaging(thread) == NULL)
    {
        if (rawSyscallNumber == SYS_RAW_EXIT)
            KiAbortRawExit(thread, arg0, EC_INVALID_STATE, "Bootstrap raw exit missing staging");

        klog(KLOG_LEVEL_ERROR,
             KE_USER_BOOTSTRAP_LOG_INVALID_SYSCALL " nr=%lu thread=%u missing bootstrap staging\n",
             (unsigned long)rawSyscallNumber,
             thread ? thread->ThreadId : 0U);
        return KiEncodeRawSyscallStatus(EC_INVALID_STATE);
    }

    switch (rawSyscallNumber)
    {
    case SYS_RAW_WRITE:
        return KiHandleRawWrite(arg0, arg1);
    case SYS_RAW_EXIT:
        return KiHandleRawExit(arg0);
    default:
        klog(KLOG_LEVEL_WARNING,
             KE_USER_BOOTSTRAP_LOG_INVALID_SYSCALL " nr=%lu thread=%u args=(%p,%p,%p)\n",
             (unsigned long)rawSyscallNumber,
             thread->ThreadId,
             (void *)(uint64_t)arg0,
             (void *)(uint64_t)arg1,
             (void *)(uint64_t)arg2);
        return KiEncodeRawSyscallStatus(EC_NOT_SUPPORTED);
    }
}

static void
KiHandleRawSyscallTrap(void *frame, MAYBE_UNUSED void *context)
{
    INTERRUPT_FRAME *interruptFrame = (INTERRUPT_FRAME *)frame;
    int64_t returnValue = KiDispatchRawSyscall(interruptFrame->Context.RAX,
                                               interruptFrame->Context.RDI,
                                               interruptFrame->Context.RSI,
                                               interruptFrame->Context.RDX);
    interruptFrame->Context.RAX = (uint64_t)returnValue;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserBootstrapRawSyscallInit(void)
{
    if (gKeUserBootstrapRawSyscallInitialized)
        return EC_SUCCESS;

    HO_STATUS status = IdtRegisterInterruptHandler(KE_USER_BOOTSTRAP_SYSCALL_VECTOR, KiHandleRawSyscallTrap, NULL);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR,
             "[USERBOOT] failed to register raw syscall vector=%u status=%s (%d)\n",
             KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
             KrGetStatusMessage(status),
             status);
        return status;
    }

    gKeUserBootstrapRawSyscallInitialized = TRUE;
    klog(KLOG_LEVEL_INFO, "[USERBOOT] raw syscall trap ready vector=%u\n", KE_USER_BOOTSTRAP_SYSCALL_VECTOR);
    return EC_SUCCESS;
}
