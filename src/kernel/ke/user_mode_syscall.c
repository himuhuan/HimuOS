/**
 * HimuOperatingSystem
 *
 * File: ke/user_mode_syscall.c
 * Description: Minimal user syscall trap, raw dispatcher, and user-copy helpers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <arch/amd64/idt.h>
#include <arch/amd64/pm.h>
#include <kernel/ex/ex_syscall.h>
#include <kernel/ex/user_regression_anchors.h>
#include <kernel/ex/user_syscall_abi.h>
#include <kernel/hodbg.h>
#include <kernel/ke/console.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_mode.h>
#include <libc/string.h>

static BOOL gKeUserModeRawSyscallInitialized;

static HO_STATUS KiValidateUserModeRange(const KE_USER_MODE_LAYOUT *layout,
                                              HO_VIRTUAL_ADDRESS userBase,
                                              uint64_t length,
                                              HO_VIRTUAL_ADDRESS *outEndExclusive);
static HO_STATUS KiValidateUserModePages(const KE_KERNEL_ADDRESS_SPACE *space,
                                              HO_VIRTUAL_ADDRESS userBase,
                                              HO_VIRTUAL_ADDRESS endExclusive,
                                              BOOL requireWritable);
static void KiHandleRawSyscallTrap(void *frame, void *context);

static HO_STATUS
KiValidateUserModeRange(const KE_USER_MODE_LAYOUT *layout,
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
KiValidateUserModePages(const KE_KERNEL_ADDRESS_SPACE *space,
                             HO_VIRTUAL_ADDRESS userBase,
                             HO_VIRTUAL_ADDRESS endExclusive,
                             BOOL requireWritable)
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
                 EX_USER_REGRESSION_LOG_INVALID_USER_BUFFER " query failed addr=%p status=%s (%d)\n",
                 (void *)(uint64_t)pageBase,
                 KrGetStatusMessage(status),
                 status);
            return status;
        }

        if (!mapping.Present || !mapping.UserAccessible)
        {
            klog(KLOG_LEVEL_WARNING,
                 EX_USER_REGRESSION_LOG_INVALID_USER_BUFFER " addr=%p present=%u userReachable=%u attrs=%p\n",
                 (void *)(uint64_t)pageBase,
                 mapping.Present,
                 mapping.UserAccessible,
                 (void *)(uint64_t)mapping.Attributes);
            return EC_ILLEGAL_ARGUMENT;
        }

        if (requireWritable && (mapping.Attributes & PTE_WRITABLE) == 0)
        {
            klog(KLOG_LEVEL_WARNING,
                 EX_USER_REGRESSION_LOG_INVALID_USER_BUFFER " addr=%p missing-writable attrs=%p\n",
                 (void *)(uint64_t)pageBase,
                 (void *)(uint64_t)mapping.Attributes);
            return EC_ILLEGAL_ARGUMENT;
        }

        if (pageBase == lastPageBase)
            break;

        pageBase += PAGE_4KB;
    }

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserModeCopyInBytes(void *kernelDestination, HO_VIRTUAL_ADDRESS userSource, uint64_t length)
{
    if (!kernelDestination)
        return EC_ILLEGAL_ARGUMENT;

    if (length == 0)
        return EC_SUCCESS;

    KE_USER_MODE_LAYOUT layout = {0};
    HO_STATUS status = KeUserModeQueryCurrentThreadLayout(&layout);
    if (status != EC_SUCCESS)
        return status;

    HO_VIRTUAL_ADDRESS endExclusive = 0;
    status = KiValidateUserModeRange(&layout, userSource, length, &endExclusive);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING,
             EX_USER_REGRESSION_LOG_INVALID_USER_BUFFER " range addr=%p len=%lu status=%s (%d)\n",
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
             EX_USER_REGRESSION_LOG_INVALID_USER_BUFFER " active-root=%p owner-root=%p addr=%p len=%lu\n",
             (void *)(uint64_t)activeRoot,
             (void *)(uint64_t)layout.OwnerRootPageTablePhys,
             (void *)(uint64_t)userSource,
             (unsigned long)length);
        return EC_INVALID_STATE;
    }

    KE_KERNEL_ADDRESS_SPACE ownerView = {0};
    ownerView.RootPageTablePhys = layout.OwnerRootPageTablePhys;
    ownerView.Initialized = TRUE;

    status = KiValidateUserModePages(&ownerView, userSource, endExclusive, FALSE);
    if (status != EC_SUCCESS)
        return status;

    memcpy(kernelDestination, (const void *)(uint64_t)userSource, (size_t)length);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserModeCopyOutBytes(HO_VIRTUAL_ADDRESS userDestination, const void *kernelSource, uint64_t length)
{
    if (kernelSource == NULL && length != 0)
        return EC_ILLEGAL_ARGUMENT;

    if (length == 0)
        return EC_SUCCESS;

    KE_USER_MODE_LAYOUT layout = {0};
    HO_STATUS status = KeUserModeQueryCurrentThreadLayout(&layout);
    if (status != EC_SUCCESS)
        return status;

    HO_VIRTUAL_ADDRESS endExclusive = 0;
    status = KiValidateUserModeRange(&layout, userDestination, length, &endExclusive);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_WARNING,
             EX_USER_REGRESSION_LOG_INVALID_USER_BUFFER " copyout range addr=%p len=%lu status=%s (%d)\n",
             (void *)(uint64_t)userDestination,
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
        return EC_INVALID_STATE;

    KE_KERNEL_ADDRESS_SPACE ownerView = {
        .RootPageTablePhys = layout.OwnerRootPageTablePhys,
        .Initialized = TRUE,
    };

    status = KiValidateUserModePages(&ownerView, userDestination, endExclusive, TRUE);
    if (status != EC_SUCCESS)
        return status;

    memcpy((void *)(uint64_t)userDestination, kernelSource, (size_t)length);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserModeWriteConsoleBytes(const char *bytes, uint64_t length, uint64_t *outWritten)
{
    if (outWritten == NULL)
        return EC_ILLEGAL_ARGUMENT;

    *outWritten = 0;

    if (length == 0)
        return EC_SUCCESS;

    if (bytes == NULL)
        return EC_ILLEGAL_ARGUMENT;

    for (uint64_t index = 0; index < length; ++index)
    {
        if (ConsoleWriteChar(bytes[index]) < 0)
            return EC_FAILURE;

        ++(*outWritten);
    }

    return EC_SUCCESS;
}

static void
KiHandleRawSyscallTrap(void *frame, MAYBE_UNUSED void *context)
{
    INTERRUPT_FRAME *interruptFrame = (INTERRUPT_FRAME *)frame;
    EX_SYSCALL_ARGUMENTS args = {
        .Number = interruptFrame->Context.RAX,
        .Arg0 = interruptFrame->Context.RDI,
        .Arg1 = interruptFrame->Context.RSI,
        .Arg2 = interruptFrame->Context.RDX,
    };
    EX_SYSCALL_DISPATCH_RESULT result = {0};

    HO_STATUS status = ExDispatchSyscall(&args, &result);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Ex syscall dispatcher failed");

    if (result.Disposition == EX_SYSCALL_DISPOSITION_EXIT_CURRENT_THREAD)
        KeThreadExit();

    if (result.Disposition != EX_SYSCALL_DISPOSITION_RETURN_TO_USER)
        HO_KPANIC(EC_INVALID_STATE, "Ex syscall dispatcher returned an invalid disposition");

    interruptFrame->Context.RAX = (uint64_t)result.ReturnValue;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserModeRawSyscallInit(void)
{
    if (gKeUserModeRawSyscallInitialized)
        return EC_SUCCESS;

    HO_STATUS status = IdtRegisterInterruptHandler(EX_USER_SYSCALL_VECTOR, KiHandleRawSyscallTrap, NULL);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR,
             "[USERBOOT] failed to register raw syscall vector=%u status=%s (%d)\n",
             EX_USER_SYSCALL_VECTOR,
             KrGetStatusMessage(status),
             status);
        return status;
    }

    gKeUserModeRawSyscallInitialized = TRUE;
    klog(KLOG_LEVEL_INFO, "[USERBOOT] raw syscall trap ready vector=%u\n", EX_USER_SYSCALL_VECTOR);
    return EC_SUCCESS;
}
