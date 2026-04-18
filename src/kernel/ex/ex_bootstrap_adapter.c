/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_adapter.c
 * Description: Thin Ex bootstrap adapter callback bridge.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ke/bootstrap_callbacks.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/sysinfo.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>

static int64_t KiEncodeCapabilitySyscallStatus(HO_STATUS status);
static int64_t KiRejectCapabilitySyscall(const char *operation,
                                         uint64_t syscallNumber,
                                         EX_PRIVATE_HANDLE handle,
                                         HO_STATUS status);
static int64_t KiHandleCapabilityWrite(EX_PROCESS *process,
                                       EX_PRIVATE_HANDLE handle,
                                       uint64_t userBuffer,
                                       uint64_t length);
static int64_t KiHandleCapabilityClose(EX_PROCESS *process, EX_PRIVATE_HANDLE handle);
static HO_STATUS KiDecodeCapabilityWaitTimeoutNs(uint64_t timeoutMsRaw, uint64_t reserved, uint64_t *outTimeoutNs);
static int64_t KiHandleCapabilityWaitOne(EX_PROCESS *process,
                                         EX_PRIVATE_HANDLE handle,
                                         uint64_t timeoutMsRaw,
                                         uint64_t reserved);
static void KiCopyAbiString(char *destination, size_t destinationSize, const char *source);
static BOOL KiAppendSysinfoChar(char *buffer, size_t *offset, size_t capacity, char value);
static BOOL KiAppendSysinfoLiteral(char *buffer, size_t *offset, size_t capacity, const char *literal);
static BOOL KiAppendSysinfoUInt64(char *buffer, size_t *offset, size_t capacity, uint64_t value);
static BOOL KiAppendSysinfoHex64(char *buffer, size_t *offset, size_t capacity, uint64_t value);
static BOOL KiAppendSysinfoVirtualAddress(char *buffer, size_t *offset, size_t capacity, HO_VIRTUAL_ADDRESS value);
static BOOL KiAppendSysinfoPadding(char *buffer, size_t *offset, size_t capacity, size_t count);
static BOOL KiAppendSysinfoPaddedLiteral(
    char *buffer, size_t *offset, size_t capacity, const char *literal, size_t width);
static BOOL KiAppendSysinfoPaddedUInt64(char *buffer, size_t *offset, size_t capacity, uint64_t value, size_t width);
static BOOL KiAppendSysinfoMegabytes(char *buffer, size_t *offset, size_t capacity, uint64_t bytes);
static BOOL KiAppendSysinfoUptimeTenths(char *buffer, size_t *offset, size_t capacity, uint64_t nanoseconds);
static BOOL KiAppendSysinfoScaledFrequency(char *buffer, size_t *offset, size_t capacity, uint64_t frequencyHz);
static BOOL KiAppendSysinfoAddressLine(
    char *buffer, size_t *offset, size_t capacity, const char *label, HO_VIRTUAL_ADDRESS address);
static BOOL KiAppendSysinfoRangeLine(char *buffer,
                                     size_t *offset,
                                     size_t capacity,
                                     const char *label,
                                     HO_VIRTUAL_ADDRESS base,
                                     HO_VIRTUAL_ADDRESS endExclusive);
static BOOL KiAppendSysinfoArenaLine(char *buffer,
                                     size_t *offset,
                                     size_t capacity,
                                     const char *label,
                                     HO_VIRTUAL_ADDRESS base,
                                     uint64_t usedPages,
                                     uint64_t totalPages,
                                     uint64_t activeAllocations);
static BOOL KiAppendSysinfoFixmapLine(char *buffer,
                                      size_t *offset,
                                      size_t capacity,
                                      HO_VIRTUAL_ADDRESS base,
                                      uint64_t activeSlots,
                                      uint64_t totalSlots,
                                      uint64_t activeAllocations);
static const char *KiGetImportedRegionTypeName(uint16_t type);
static HO_STATUS KiCaptureSysinfoOverview(EX_SYSINFO_OVERVIEW *overview);
static HO_STATUS KiCaptureSysinfoThreadList(EX_SYSINFO_THREAD_LIST *threadList);
static HO_STATUS KiBuildSysinfoOverviewText(char *buffer,
                                            size_t capacity,
                                            const EX_SYSINFO_OVERVIEW *overview,
                                            size_t *outLength);
static HO_STATUS KiBuildSysinfoMemmapText(char *buffer, size_t capacity, size_t *outLength);
static const char *KiGetSysinfoThreadStateName(uint32_t state);
static HO_STATUS KiBuildSysinfoThreadListText(char *buffer,
                                              size_t capacity,
                                              const EX_SYSINFO_THREAD_LIST *threadList,
                                              size_t *outLength);
static int64_t KiRejectQuerySysinfo(uint64_t infoClassRaw, uint64_t userBuffer, uint64_t length, HO_STATUS status);
static int64_t KiHandleQuerySysinfo(EX_PROCESS *process, uint64_t infoClassRaw, uint64_t userBuffer, uint64_t length);
static HO_STATUS KiDestroyBootstrapWrapperObjects(KTHREAD *thread);
static HO_STATUS KiValidateBootstrapTerminationHandoff(KTHREAD *thread,
                                                       EX_THREAD **outRuntimeThread,
                                                       EX_PROCESS **outProcess);
static const char *KiGetBootstrapUserExceptionShortName(uint8_t vectorNumber);
static void KiLogBootstrapPageFaultBits(uint32_t errorCode);
static void KiLogBootstrapUserExceptionEvidence(KTHREAD *thread,
                                                const EX_PROCESS *process,
                                                const KE_BOOTSTRAP_USER_EXCEPTION_CONTEXT *context);
static HO_NORETURN void ExBootstrapUserExceptionCallback(KTHREAD *thread,
                                                         const KE_BOOTSTRAP_USER_EXCEPTION_CONTEXT *context);

static int64_t
KiEncodeCapabilitySyscallStatus(HO_STATUS status)
{
    return status == EC_SUCCESS ? 0 : -(int64_t)status;
}

static int64_t
KiRejectCapabilitySyscall(const char *operation, uint64_t syscallNumber, EX_PRIVATE_HANDLE handle, HO_STATUS status)
{
    KTHREAD *thread = KeGetCurrentThread();

    klog(KLOG_LEVEL_WARNING, KE_USER_BOOTSTRAP_LOG_CAP_REJECTED " op=%s nr=%lu handle=%u thread=%u status=%s (%d)\n",
         operation, (unsigned long)syscallNumber, handle, thread ? thread->ThreadId : 0U, KrGetStatusMessage(status),
         status);

    return KiEncodeCapabilitySyscallStatus(status);
}

static int64_t
KiHandleCapabilityWrite(EX_PROCESS *process, EX_PRIVATE_HANDLE handle, uint64_t userBuffer, uint64_t length)
{
    EX_OBJECT_HEADER *objectHeader = NULL;
    KTHREAD *thread = KeGetCurrentThread();
    uint64_t written = 0;
    char scratch[KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH];

    if (process == NULL)
        return KiRejectCapabilitySyscall("SYS_WRITE", SYS_WRITE, handle, EC_INVALID_STATE);

    if (length > KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH)
    {
        klog(KLOG_LEVEL_WARNING, KE_USER_BOOTSTRAP_LOG_INVALID_USER_BUFFER " cap=write addr=%p len=%lu exceeds=%u\n",
             (void *)(uint64_t)userBuffer, (unsigned long)length, KE_USER_BOOTSTRAP_SYS_RAW_WRITE_MAX_LENGTH);
        return KiRejectCapabilitySyscall("SYS_WRITE", SYS_WRITE, handle, EC_ILLEGAL_ARGUMENT);
    }

    HO_STATUS status = ExBootstrapResolvePrivateHandle(process, handle, EX_OBJECT_TYPE_STDOUT_SERVICE,
                                                       EX_PRIVATE_HANDLE_RIGHT_WRITE, &objectHeader);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_WRITE", SYS_WRITE, handle, status);

    if (length != 0)
    {
        status = KeUserBootstrapCopyInBytes(scratch, (HO_VIRTUAL_ADDRESS)userBuffer, length);
        if (status == EC_SUCCESS)
            status = KeUserBootstrapWriteConsoleBytes(scratch, length, &written);
    }

    HO_STATUS releaseStatus = ExBootstrapReleaseResolvedObject(objectHeader);
    if (status == EC_SUCCESS && releaseStatus != EC_SUCCESS)
        status = releaseStatus;

    if (status != EC_SUCCESS)
    {
        if (status == EC_FAILURE)
        {
            klog(KLOG_LEVEL_ERROR, "[USERCAP] SYS_WRITE console emit failed index=%lu thread=%u handle=%u\n",
                 (unsigned long)written, thread ? thread->ThreadId : 0U, handle);
            return KiEncodeCapabilitySyscallStatus(status);
        }

        return KiRejectCapabilitySyscall("SYS_WRITE", SYS_WRITE, handle, status);
    }

    klog(KLOG_LEVEL_INFO, KE_USER_BOOTSTRAP_LOG_CAP_WRITE_SUCCEEDED " bytes=%lu thread=%u handle=%u\n",
         (unsigned long)written, thread ? thread->ThreadId : 0U, handle);

    return (int64_t)written;
}

static int64_t
KiHandleCapabilityClose(EX_PROCESS *process, EX_PRIVATE_HANDLE handle)
{
    KTHREAD *thread = KeGetCurrentThread();

    if (process == NULL)
        return KiRejectCapabilitySyscall("SYS_CLOSE", SYS_CLOSE, handle, EC_INVALID_STATE);

    if (handle == EX_PRIVATE_HANDLE_INVALID)
        return KiRejectCapabilitySyscall("SYS_CLOSE", SYS_CLOSE, handle, EC_ILLEGAL_ARGUMENT);

    EX_PRIVATE_HANDLE localHandle = handle;
    HO_STATUS status = ExBootstrapClosePrivateHandle(process, &localHandle);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_CLOSE", SYS_CLOSE, handle, status);

    klog(KLOG_LEVEL_INFO, KE_USER_BOOTSTRAP_LOG_CAP_CLOSE_SUCCEEDED " handle=%u thread=%u\n", handle,
         thread ? thread->ThreadId : 0U);

    return 0;
}

static HO_STATUS
KiDecodeCapabilityWaitTimeoutNs(uint64_t timeoutMsRaw, uint64_t reserved, uint64_t *outTimeoutNs)
{
    if (outTimeoutNs == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (reserved != 0 || timeoutMsRaw > KE_USER_BOOTSTRAP_WAIT_ONE_TIMEOUT_MAX_MS)
        return EC_ILLEGAL_ARGUMENT;

    *outTimeoutNs = timeoutMsRaw * KE_USER_BOOTSTRAP_WAIT_ONE_TIMEOUT_NS_PER_MS;
    return EC_SUCCESS;
}

static int64_t
KiHandleCapabilityWaitOne(EX_PROCESS *process, EX_PRIVATE_HANDLE handle, uint64_t timeoutMsRaw, uint64_t reserved)
{
    EX_OBJECT_HEADER *objectHeader = NULL;
    EX_WAITABLE_OBJECT *waitObject = NULL;
    KTHREAD *thread = KeGetCurrentThread();
    uint64_t timeoutNs = 0;

    if (process == NULL)
        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", SYS_WAIT_ONE, handle, EC_INVALID_STATE);

    if (handle == EX_PRIVATE_HANDLE_INVALID)
        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", SYS_WAIT_ONE, handle, EC_ILLEGAL_ARGUMENT);

    HO_STATUS status = KiDecodeCapabilityWaitTimeoutNs(timeoutMsRaw, reserved, &timeoutNs);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", SYS_WAIT_ONE, handle, status);

    status = ExBootstrapResolvePrivateHandle(process, handle, EX_OBJECT_TYPE_WAITABLE, EX_PRIVATE_HANDLE_RIGHT_WAIT,
                                             &objectHeader);
    if (status != EC_SUCCESS)
        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", SYS_WAIT_ONE, handle, status);

    waitObject = CONTAINING_RECORD(objectHeader, EX_WAITABLE_OBJECT, Header);
    if (waitObject->Dispatcher == NULL)
    {
        status = EC_INVALID_STATE;
    }
    else
    {
        status = KeWaitForSingleObject(waitObject->Dispatcher, timeoutNs);
    }

    HO_STATUS releaseStatus = ExBootstrapReleaseResolvedObject(objectHeader);
    if ((status == EC_SUCCESS || status == EC_TIMEOUT) && releaseStatus != EC_SUCCESS)
        status = releaseStatus;

    if (status != EC_SUCCESS)
    {
        if (status == EC_TIMEOUT)
            return KiEncodeCapabilitySyscallStatus(status);

        return KiRejectCapabilitySyscall("SYS_WAIT_ONE", SYS_WAIT_ONE, handle, status);
    }

    klog(KLOG_LEVEL_INFO, KE_USER_BOOTSTRAP_LOG_CAP_WAIT_SUCCEEDED " handle=%u thread=%u timeout_ms=%lu\n", handle,
         thread ? thread->ThreadId : 0U, (unsigned long)timeoutMsRaw);

    return 0;
}

static void
KiCopyAbiString(char *destination, size_t destinationSize, const char *source)
{
    size_t copyLength = 0;

    if (destination == NULL || destinationSize == 0)
        return;

    if (source != NULL)
    {
        copyLength = strlen(source);
        if (copyLength >= destinationSize)
            copyLength = destinationSize - 1;
        memcpy(destination, source, copyLength);
    }

    destination[copyLength] = '\0';
}

static BOOL
KiAppendSysinfoChar(char *buffer, size_t *offset, size_t capacity, char value)
{
    if (buffer == NULL || offset == NULL || *offset >= capacity)
        return FALSE;

    buffer[*offset] = value;
    *offset += 1;
    return TRUE;
}

static BOOL
KiAppendSysinfoLiteral(char *buffer, size_t *offset, size_t capacity, const char *literal)
{
    size_t literalLength;

    if (literal == NULL)
        return FALSE;

    literalLength = strlen(literal);
    if (buffer == NULL || offset == NULL || literalLength > (capacity - *offset))
        return FALSE;

    memcpy(buffer + *offset, literal, literalLength);
    *offset += literalLength;
    return TRUE;
}

static BOOL
KiAppendSysinfoUInt64(char *buffer, size_t *offset, size_t capacity, uint64_t value)
{
    char digits[21];
    uint64_t length = UInt64ToStringEx(value, digits, 10, 0, 0);

    if (length > (capacity - *offset))
        return FALSE;

    memcpy(buffer + *offset, digits, (size_t)length);
    *offset += (size_t)length;
    return TRUE;
}

static BOOL
KiAppendSysinfoHex64(char *buffer, size_t *offset, size_t capacity, uint64_t value)
{
    char digits[17];
    size_t length = (size_t)UInt64ToStringEx(value, digits, 16, 16, '0');

    if (length != 16U)
        return FALSE;

    for (size_t index = 0; index < length; ++index)
    {
        if (!KiAppendSysinfoChar(buffer, offset, capacity, digits[index]))
            return FALSE;

        if (index != (length - 1U) && ((index + 1U) % 4U) == 0U)
        {
            if (!KiAppendSysinfoChar(buffer, offset, capacity, '_'))
                return FALSE;
        }
    }

    return TRUE;
}

static BOOL
KiAppendSysinfoVirtualAddress(char *buffer, size_t *offset, size_t capacity, HO_VIRTUAL_ADDRESS value)
{
    return KiAppendSysinfoHex64(buffer, offset, capacity, (uint64_t)value);
}

static BOOL
KiAppendSysinfoPadding(char *buffer, size_t *offset, size_t capacity, size_t count)
{
    while (count != 0U)
    {
        if (!KiAppendSysinfoChar(buffer, offset, capacity, ' '))
            return FALSE;
        --count;
    }

    return TRUE;
}

static BOOL
KiAppendSysinfoPaddedLiteral(char *buffer, size_t *offset, size_t capacity, const char *literal, size_t width)
{
    size_t literalLength = 0;

    if (literal == NULL)
        return FALSE;

    literalLength = strlen(literal);
    if (!KiAppendSysinfoLiteral(buffer, offset, capacity, literal))
        return FALSE;

    if (literalLength < width)
        return KiAppendSysinfoPadding(buffer, offset, capacity, width - literalLength);

    return TRUE;
}

static BOOL
KiAppendSysinfoPaddedUInt64(char *buffer, size_t *offset, size_t capacity, uint64_t value, size_t width)
{
    char digits[21];
    size_t length = (size_t)UInt64ToStringEx(value, digits, 10, 0, 0);

    if (length > (capacity - *offset))
        return FALSE;

    memcpy(buffer + *offset, digits, length);
    *offset += length;

    if (length < width)
        return KiAppendSysinfoPadding(buffer, offset, capacity, width - length);

    return TRUE;
}

static BOOL
KiAppendSysinfoMegabytes(char *buffer, size_t *offset, size_t capacity, uint64_t bytes)
{
    return KiAppendSysinfoUInt64(buffer, offset, capacity, bytes / (1024ULL * 1024ULL)) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, " MB");
}

static BOOL
KiAppendSysinfoUptimeTenths(char *buffer, size_t *offset, size_t capacity, uint64_t nanoseconds)
{
    uint64_t tenths = nanoseconds / 100000000ULL;

    return KiAppendSysinfoUInt64(buffer, offset, capacity, tenths / 10ULL) &&
           KiAppendSysinfoChar(buffer, offset, capacity, '.') &&
           KiAppendSysinfoChar(buffer, offset, capacity, (char)('0' + (tenths % 10ULL))) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, " s");
}

static BOOL
KiAppendSysinfoScaledFrequency(char *buffer, size_t *offset, size_t capacity, uint64_t frequencyHz)
{
    if (frequencyHz >= 1000000000ULL)
    {
        uint64_t whole = frequencyHz / 1000000000ULL;
        uint64_t tenth = (frequencyHz % 1000000000ULL) / 100000000ULL;

        if (!KiAppendSysinfoUInt64(buffer, offset, capacity, whole))
            return FALSE;
        if (tenth != 0U)
        {
            if (!KiAppendSysinfoChar(buffer, offset, capacity, '.') ||
                !KiAppendSysinfoChar(buffer, offset, capacity, (char)('0' + tenth)))
                return FALSE;
        }
        return KiAppendSysinfoLiteral(buffer, offset, capacity, " GHz");
    }

    if (frequencyHz >= 1000000ULL)
    {
        uint64_t whole = frequencyHz / 1000000ULL;
        uint64_t tenth = (frequencyHz % 1000000ULL) / 100000ULL;

        if (!KiAppendSysinfoUInt64(buffer, offset, capacity, whole))
            return FALSE;
        if (tenth != 0U)
        {
            if (!KiAppendSysinfoChar(buffer, offset, capacity, '.') ||
                !KiAppendSysinfoChar(buffer, offset, capacity, (char)('0' + tenth)))
                return FALSE;
        }
        return KiAppendSysinfoLiteral(buffer, offset, capacity, " MHz");
    }

    return KiAppendSysinfoUInt64(buffer, offset, capacity, frequencyHz) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, " Hz");
}

static BOOL
KiAppendSysinfoAddressLine(char *buffer, size_t *offset, size_t capacity, const char *label, HO_VIRTUAL_ADDRESS address)
{
    return KiAppendSysinfoLiteral(buffer, offset, capacity, "  ") &&
           KiAppendSysinfoPaddedLiteral(buffer, offset, capacity, label, 13U) &&
           KiAppendSysinfoVirtualAddress(buffer, offset, capacity, address) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, "\n");
}

static BOOL
KiAppendSysinfoRangeLine(char *buffer,
                         size_t *offset,
                         size_t capacity,
                         const char *label,
                         HO_VIRTUAL_ADDRESS base,
                         HO_VIRTUAL_ADDRESS endExclusive)
{
    return KiAppendSysinfoLiteral(buffer, offset, capacity, "  ") &&
           KiAppendSysinfoPaddedLiteral(buffer, offset, capacity, label, 13U) &&
           KiAppendSysinfoChar(buffer, offset, capacity, '[') &&
           KiAppendSysinfoVirtualAddress(buffer, offset, capacity, base) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, ", ") &&
           KiAppendSysinfoVirtualAddress(buffer, offset, capacity, endExclusive) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, ")\n");
}

static BOOL
KiAppendSysinfoArenaLine(char *buffer,
                         size_t *offset,
                         size_t capacity,
                         const char *label,
                         HO_VIRTUAL_ADDRESS base,
                         uint64_t usedPages,
                         uint64_t totalPages,
                         uint64_t activeAllocations)
{
    return KiAppendSysinfoLiteral(buffer, offset, capacity, "  ") &&
           KiAppendSysinfoPaddedLiteral(buffer, offset, capacity, label, 13U) &&
           KiAppendSysinfoVirtualAddress(buffer, offset, capacity, base) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, "  used ") &&
           KiAppendSysinfoUInt64(buffer, offset, capacity, usedPages) &&
           KiAppendSysinfoChar(buffer, offset, capacity, '/') &&
           KiAppendSysinfoUInt64(buffer, offset, capacity, totalPages) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, " pages allocs ") &&
           KiAppendSysinfoUInt64(buffer, offset, capacity, activeAllocations) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, "\n");
}

static BOOL
KiAppendSysinfoFixmapLine(char *buffer,
                          size_t *offset,
                          size_t capacity,
                          HO_VIRTUAL_ADDRESS base,
                          uint64_t activeSlots,
                          uint64_t totalSlots,
                          uint64_t activeAllocations)
{
    return KiAppendSysinfoLiteral(buffer, offset, capacity, "  ") &&
           KiAppendSysinfoPaddedLiteral(buffer, offset, capacity, "fixmap arena", 13U) &&
           KiAppendSysinfoVirtualAddress(buffer, offset, capacity, base) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, "  slots ") &&
           KiAppendSysinfoUInt64(buffer, offset, capacity, activeSlots) &&
           KiAppendSysinfoChar(buffer, offset, capacity, '/') &&
           KiAppendSysinfoUInt64(buffer, offset, capacity, totalSlots) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, " allocs ") &&
           KiAppendSysinfoUInt64(buffer, offset, capacity, activeAllocations) &&
           KiAppendSysinfoLiteral(buffer, offset, capacity, "\n");
}

static const char *
KiGetImportedRegionTypeName(uint16_t type)
{
    switch (type)
    {
    case BOOT_MAPPING_REGION_KERNEL_CODE:
        return "kernel-code";
    case BOOT_MAPPING_REGION_KERNEL_DATA:
        return "kernel-data";
    case BOOT_MAPPING_REGION_KERNEL_STACK:
        return "kernel-stack";
    case BOOT_MAPPING_REGION_KERNEL_IST_STACK:
        return "ist-stack";
    case BOOT_MAPPING_REGION_FRAMEBUFFER:
        return "framebuffer";
    case BOOT_MAPPING_REGION_HPET_MMIO:
        return "hpet-mmio";
    case BOOT_MAPPING_REGION_LAPIC_MMIO:
        return "lapic-mmio";
    default:
        return NULL;
    }
}

static HO_STATUS
KiCaptureSysinfoOverview(EX_SYSINFO_OVERVIEW *overview)
{
    if (overview == NULL)
        return EC_ILLEGAL_ARGUMENT;

    memset(overview, 0, sizeof(*overview));
    overview->Version = EX_SYSINFO_OVERVIEW_VERSION;
    overview->Size = sizeof(*overview);

    {
        ARCH_BASIC_CPU_INFO cpu = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_CPU_BASIC, &cpu, sizeof(cpu), NULL) == EC_SUCCESS)
        {
            overview->ValidMask |= EX_SYSINFO_OVERVIEW_VALID_CPU;
            KiCopyAbiString(overview->CpuModel, sizeof(overview->CpuModel), cpu.ModelName);
        }
    }

    {
        SYSINFO_PHYSICAL_MEM_STATS physical = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_PHYSICAL_MEM_STATS, &physical, sizeof(physical), NULL) == EC_SUCCESS)
        {
            overview->ValidMask |= EX_SYSINFO_OVERVIEW_VALID_MEMORY;
            overview->PhysicalTotalBytes = physical.TotalBytes;
            overview->PhysicalFreeBytes = physical.FreeBytes;
            overview->PhysicalAllocatedBytes = physical.AllocatedBytes;
            overview->PhysicalReservedBytes = physical.ReservedBytes;
        }
    }

    {
        SYSINFO_VMM_OVERVIEW vmm = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_VMM_OVERVIEW, &vmm, sizeof(vmm), NULL) == EC_SUCCESS)
        {
            overview->ValidMask |= EX_SYSINFO_OVERVIEW_VALID_VMM;
            overview->StackArenaTotalPages = vmm.StackArena.TotalPages;
            overview->StackArenaUsedPages = vmm.StackArena.TotalPages - vmm.StackArena.FreePages;
            overview->HeapArenaTotalPages = vmm.HeapArena.TotalPages;
            overview->HeapArenaUsedPages = vmm.HeapArena.TotalPages - vmm.HeapArena.FreePages;
            overview->FixmapTotalSlots = vmm.FixmapTotalSlots;
            overview->FixmapActiveSlots = vmm.FixmapActiveSlots;
        }
    }

    {
        KE_SYSINFO_SCHEDULER_DATA scheduler = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_SCHEDULER, &scheduler, sizeof(scheduler), NULL) == EC_SUCCESS)
        {
            overview->ValidMask |= EX_SYSINFO_OVERVIEW_VALID_SCHEDULER;
            overview->SchedulerEnabled = scheduler.SchedulerEnabled ? 1U : 0U;
            overview->CurrentThreadId = scheduler.CurrentThreadId;
            overview->IdleThreadId = scheduler.IdleThreadId;
            overview->ReadyQueueDepth = scheduler.ReadyQueueDepth;
            overview->SleepQueueDepth = scheduler.SleepQueueDepth;
            overview->ActiveThreadCount = scheduler.ActiveThreadCount;
        }
    }

    {
        SYSINFO_UPTIME uptime = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_UPTIME, &uptime, sizeof(uptime), NULL) == EC_SUCCESS)
        {
            overview->ValidMask |= EX_SYSINFO_OVERVIEW_VALID_UPTIME;
            overview->UptimeNanoseconds = uptime.Nanoseconds;
        }
    }

    {
        SYSINFO_CLOCK_EVENT clockEvent = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_CLOCK_EVENT, &clockEvent, sizeof(clockEvent), NULL) == EC_SUCCESS)
        {
            overview->ValidMask |= EX_SYSINFO_OVERVIEW_VALID_CLOCK;
            overview->ClockReady = clockEvent.Ready ? 1U : 0U;
            overview->ClockVectorNumber = clockEvent.VectorNumber;
            overview->ClockFrequencyHz = clockEvent.FreqHz;
            KiCopyAbiString(overview->ClockSourceName, sizeof(overview->ClockSourceName), clockEvent.SourceName);
        }
    }

    {
        SYSINFO_TIME_SOURCE timeSource = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_TIME_SOURCE, &timeSource, sizeof(timeSource), NULL) == EC_SUCCESS)
        {
            overview->ValidMask |= EX_SYSINFO_OVERVIEW_VALID_TIME_SOURCE;
            overview->TimeSourceFrequencyHz = timeSource.Frequency;
            KiCopyAbiString(overview->TimeSourceName, sizeof(overview->TimeSourceName), timeSource.Name);
        }
    }

    {
        SYSINFO_SYSTEM_VERSION version = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_SYSTEM_VERSION, &version, sizeof(version), NULL) == EC_SUCCESS)
        {
            overview->ValidMask |= EX_SYSINFO_OVERVIEW_VALID_VERSION;
            overview->SystemMajor = version.Major;
            overview->SystemMinor = version.Minor;
            overview->SystemPatch = version.Patch;
            KiCopyAbiString(overview->BuildDate, sizeof(overview->BuildDate), version.BuildDate);
            KiCopyAbiString(overview->BuildTime, sizeof(overview->BuildTime), version.BuildTime);
        }
    }

    return EC_SUCCESS;
}

static HO_STATUS
KiCaptureSysinfoThreadList(EX_SYSINFO_THREAD_LIST *threadList)
{
    EX_SYSINFO_THREAD_LIST userThreads = {0};
    BOOL includeIdleThread = FALSE;

    if (threadList == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = ExBootstrapCaptureThreadList(&userThreads);
    if (status != EC_SUCCESS)
        return status;

    memset(threadList, 0, sizeof(*threadList));
    threadList->Version = EX_SYSINFO_THREAD_LIST_VERSION;
    threadList->Size = sizeof(*threadList);

    {
        KE_SYSINFO_SCHEDULER_DATA scheduler = {0};
        if (KeQuerySystemInformation(KE_SYSINFO_SCHEDULER, &scheduler, sizeof(scheduler), NULL) == EC_SUCCESS &&
            scheduler.SchedulerEnabled)
        {
            includeIdleThread = TRUE;
            threadList->TotalCount = userThreads.TotalCount + 1U;

            if (threadList->ReturnedCount < EX_SYSINFO_THREAD_LIST_MAX_ENTRIES)
            {
                EX_SYSINFO_THREAD_ENTRY *idleEntry = &threadList->Entries[threadList->ReturnedCount++];
                idleEntry->ThreadId = scheduler.IdleThreadId;
                idleEntry->State = EX_SYSINFO_THREAD_STATE_IDLE;
                idleEntry->Priority = KTHREAD_DEFAULT_PRIORITY;
                KiCopyAbiString(idleEntry->Name, sizeof(idleEntry->Name), "idle");
            }
            else
            {
                threadList->Truncated = TRUE;
            }
        }
    }

    if (!includeIdleThread)
        threadList->TotalCount = userThreads.TotalCount;

    for (uint32_t index = 0; index < userThreads.ReturnedCount; ++index)
    {
        if (threadList->ReturnedCount >= EX_SYSINFO_THREAD_LIST_MAX_ENTRIES)
        {
            threadList->Truncated = TRUE;
            break;
        }

        threadList->Entries[threadList->ReturnedCount++] = userThreads.Entries[index];
    }

    if (userThreads.Truncated)
        threadList->Truncated = TRUE;

    return EC_SUCCESS;
}

static HO_STATUS
KiBuildSysinfoOverviewText(char *buffer, size_t capacity, const EX_SYSINFO_OVERVIEW *overview, size_t *outLength)
{
    size_t length = 0;

    if (buffer == NULL || overview == NULL || outLength == NULL || capacity == 0)
        return EC_ILLEGAL_ARGUMENT;

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "HimuOS System Information\n") ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, "CPU:      ") ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity,
                                (overview->ValidMask & EX_SYSINFO_OVERVIEW_VALID_CPU) != 0 ? overview->CpuModel
                                                                                           : "N/A") ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n") ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, "Memory:   "))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    if ((overview->ValidMask & EX_SYSINFO_OVERVIEW_VALID_MEMORY) != 0)
    {
        if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "Total ") ||
            !KiAppendSysinfoMegabytes(buffer, &length, capacity, overview->PhysicalTotalBytes) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  Free ") ||
            !KiAppendSysinfoMegabytes(buffer, &length, capacity, overview->PhysicalFreeBytes) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n") ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "          Alloc ") ||
            !KiAppendSysinfoMegabytes(buffer, &length, capacity, overview->PhysicalAllocatedBytes) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  Reserved ") ||
            !KiAppendSysinfoMegabytes(buffer, &length, capacity, overview->PhysicalReservedBytes) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n"))
        {
            return EC_NOT_ENOUGH_MEMORY;
        }
    }
    else if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "N/A\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "KVA:      "))
        return EC_NOT_ENOUGH_MEMORY;

    if ((overview->ValidMask & EX_SYSINFO_OVERVIEW_VALID_VMM) != 0)
    {
        if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "Stack ") ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->StackArenaUsedPages) ||
            !KiAppendSysinfoChar(buffer, &length, capacity, '/') ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->StackArenaTotalPages) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  Heap ") ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->HeapArenaUsedPages) ||
            !KiAppendSysinfoChar(buffer, &length, capacity, '/') ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->HeapArenaTotalPages) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n") ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "          Fixmap ") ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->FixmapActiveSlots) ||
            !KiAppendSysinfoChar(buffer, &length, capacity, '/') ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->FixmapTotalSlots) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n"))
        {
            return EC_NOT_ENOUGH_MEMORY;
        }
    }
    else if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "N/A\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "Threads:  "))
        return EC_NOT_ENOUGH_MEMORY;

    if ((overview->ValidMask & EX_SYSINFO_OVERVIEW_VALID_SCHEDULER) != 0)
    {
        if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "Active ") ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->ActiveThreadCount) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  Ready ") ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->ReadyQueueDepth) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n") ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "          Sleep ") ||
            !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->SleepQueueDepth) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n"))
        {
            return EC_NOT_ENOUGH_MEMORY;
        }
    }
    else if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "N/A\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "Uptime:   "))
        return EC_NOT_ENOUGH_MEMORY;

    if ((overview->ValidMask & EX_SYSINFO_OVERVIEW_VALID_UPTIME) != 0)
    {
        if (!KiAppendSysinfoUptimeTenths(buffer, &length, capacity, overview->UptimeNanoseconds) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n"))
        {
            return EC_NOT_ENOUGH_MEMORY;
        }
    }
    else if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "N/A\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "Clock:    "))
        return EC_NOT_ENOUGH_MEMORY;

    if ((overview->ValidMask & EX_SYSINFO_OVERVIEW_VALID_CLOCK) != 0)
    {
        if (overview->ClockReady != 0U)
        {
            if (!KiAppendSysinfoLiteral(buffer, &length, capacity, overview->ClockSourceName) ||
                !KiAppendSysinfoLiteral(buffer, &length, capacity, " @ ") ||
                !KiAppendSysinfoUInt64(buffer, &length, capacity, overview->ClockFrequencyHz) ||
                !KiAppendSysinfoLiteral(buffer, &length, capacity, " Hz\n"))
            {
                return EC_NOT_ENOUGH_MEMORY;
            }
        }
        else if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "not ready\n"))
        {
            return EC_NOT_ENOUGH_MEMORY;
        }
    }
    else if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "N/A\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "Time:     "))
        return EC_NOT_ENOUGH_MEMORY;

    if ((overview->ValidMask & EX_SYSINFO_OVERVIEW_VALID_TIME_SOURCE) != 0)
    {
        if (!KiAppendSysinfoLiteral(buffer, &length, capacity, overview->TimeSourceName) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, " @ ") ||
            !KiAppendSysinfoScaledFrequency(buffer, &length, capacity, overview->TimeSourceFrequencyHz) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n"))
        {
            return EC_NOT_ENOUGH_MEMORY;
        }
    }
    else if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "N/A\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (length >= capacity)
        return EC_NOT_ENOUGH_MEMORY;

    buffer[length] = '\0';
    *outLength = length;
    return EC_SUCCESS;
}

static HO_STATUS
KiBuildSysinfoMemmapText(char *buffer, size_t capacity, size_t *outLength)
{
    size_t length = 0;
    SYSINFO_VMM_OVERVIEW overview = {0};
    KE_KVA_ARENA_INFO stackArena = {0};
    KE_KVA_ARENA_INFO heapArena = {0};
    KE_KVA_ARENA_INFO fixmapArena = {0};
    KE_USER_BOOTSTRAP_LAYOUT layout = {0};
    const KE_KERNEL_ADDRESS_SPACE *space = NULL;
    uint32_t printedRegionCount = 0;

    if (buffer == NULL || outLength == NULL || capacity == 0)
        return EC_ILLEGAL_ARGUMENT;

    space = KeGetKernelAddressSpace();
    if (space == NULL)
        return EC_INVALID_STATE;

    HO_STATUS status = KeQuerySystemInformation(KE_SYSINFO_VMM_OVERVIEW, &overview, sizeof(overview), NULL);
    if (status != EC_SUCCESS)
        return status;

    status = KeKvaQueryArenaInfo(KE_KVA_ARENA_STACK, &stackArena);
    if (status != EC_SUCCESS)
        return status;

    status = KeKvaQueryArenaInfo(KE_KVA_ARENA_HEAP, &heapArena);
    if (status != EC_SUCCESS)
        return status;

    status = KeKvaQueryArenaInfo(KE_KVA_ARENA_FIXMAP, &fixmapArena);
    if (status != EC_SUCCESS)
        return status;

    status = KeUserBootstrapQueryCurrentThreadLayout(&layout);
    if (status != EC_SUCCESS)
        return status;

    if (layout.StackBase < layout.GuardBase || layout.StackTop < layout.StackBase ||
        layout.GuardBase < (layout.UserRangeBase + (2ULL * PAGE_4KB)))
    {
        return EC_INVALID_STATE;
    }

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "HimuOS Virtual Memory Map\n") ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, "High Half\n") ||
        !KiAppendSysinfoAddressLine(buffer, &length, capacity, "kernel base", KRNL_BASE_VA) ||
        !KiAppendSysinfoArenaLine(buffer, &length, capacity, "stack arena", stackArena.BaseAddress,
                                  stackArena.TotalPages - stackArena.FreePages, stackArena.TotalPages,
                                  stackArena.ActiveAllocations) ||
        !KiAppendSysinfoArenaLine(buffer, &length, capacity, "heap arena", heapArena.BaseAddress,
                                  heapArena.TotalPages - heapArena.FreePages, heapArena.TotalPages,
                                  heapArena.ActiveAllocations) ||
        !KiAppendSysinfoFixmapLine(buffer, &length, capacity, fixmapArena.BaseAddress, overview.FixmapActiveSlots,
                                   overview.FixmapTotalSlots, fixmapArena.ActiveAllocations) ||
        !KiAppendSysinfoAddressLine(buffer, &length, capacity, "HHDM base", HHDM_BASE_VA) ||
        !KiAppendSysinfoAddressLine(buffer, &length, capacity, "MMIO base", MMIO_BASE_VA) ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, "  active KVA   ") ||
        !KiAppendSysinfoUInt64(buffer, &length, capacity, overview.ActiveKvaRangeCount) ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, " live ranges\n") ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, "Imported Regions\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    for (uint32_t index = 0; index < space->RegionCount; ++index)
    {
        const KE_IMPORTED_REGION *region = &space->Regions[index];
        const char *regionName = KiGetImportedRegionTypeName(region->Type);

        if (regionName == NULL)
            continue;

        printedRegionCount += 1U;
        if (!KiAppendSysinfoAddressLine(buffer, &length, capacity, regionName, region->VirtualStart))
        {
            return EC_NOT_ENOUGH_MEMORY;
        }
    }

    if (printedRegionCount == 0U)
    {
        if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "  (none)\n"))
            return EC_NOT_ENOUGH_MEMORY;
    }

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "Low Half (bootstrap)\n") ||
        !KiAppendSysinfoRangeLine(buffer, &length, capacity, "code slot", layout.UserRangeBase,
                                  layout.UserRangeBase + PAGE_4KB) ||
        !KiAppendSysinfoRangeLine(buffer, &length, capacity, "const slot", layout.UserRangeBase + PAGE_4KB,
                                  layout.GuardBase) ||
        !KiAppendSysinfoRangeLine(buffer, &length, capacity, "guard page", layout.GuardBase, layout.StackBase) ||
        !KiAppendSysinfoRangeLine(buffer, &length, capacity, "stack page", layout.StackBase, layout.StackTop))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (length >= capacity)
        return EC_NOT_ENOUGH_MEMORY;

    buffer[length] = '\0';
    *outLength = length;
    return EC_SUCCESS;
}

static const char *
KiGetSysinfoThreadStateName(uint32_t state)
{
    switch (state)
    {
    case EX_SYSINFO_THREAD_STATE_READY:
        return "READY";
    case EX_SYSINFO_THREAD_STATE_RUNNING:
        return "RUNNING";
    case EX_SYSINFO_THREAD_STATE_BLOCKED:
        return "BLOCKED";
    case EX_SYSINFO_THREAD_STATE_SLEEPING:
        return "SLEEPING";
    case EX_SYSINFO_THREAD_STATE_TERMINATED:
        return "TERMINATED";
    case EX_SYSINFO_THREAD_STATE_IDLE:
        return "IDLE";
    default:
        return "UNKNOWN";
    }
}

static HO_STATUS
KiBuildSysinfoThreadListText(char *buffer, size_t capacity, const EX_SYSINFO_THREAD_LIST *threadList, size_t *outLength)
{
    size_t length = 0;

    if (buffer == NULL || threadList == NULL || outLength == NULL || capacity == 0)
        return EC_ILLEGAL_ARGUMENT;

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "PID  STATE       PRI  NAME\n") ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, "---  ----------  ---  ----\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    for (uint32_t index = 0; index < threadList->ReturnedCount; ++index)
    {
        const EX_SYSINFO_THREAD_ENTRY *entry = &threadList->Entries[index];
        const char *stateName = KiGetSysinfoThreadStateName(entry->State);

        if (!KiAppendSysinfoPaddedUInt64(buffer, &length, capacity, entry->ThreadId, 3U) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  ") ||
            !KiAppendSysinfoPaddedLiteral(buffer, &length, capacity, stateName, 10U) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  ") ||
            !KiAppendSysinfoPaddedUInt64(buffer, &length, capacity, entry->Priority, 3U) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  ") ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, entry->Name) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "\n"))
        {
            return EC_NOT_ENOUGH_MEMORY;
        }
    }

    if (length >= capacity)
        return EC_NOT_ENOUGH_MEMORY;

    buffer[length] = '\0';
    *outLength = length;
    return EC_SUCCESS;
}

static int64_t
KiRejectQuerySysinfo(uint64_t infoClassRaw, uint64_t userBuffer, uint64_t length, HO_STATUS status)
{
    KTHREAD *thread = KeGetCurrentThread();

    klog(KLOG_LEVEL_WARNING,
         KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_REJECTED " class=%lu addr=%p len=%lu thread=%u status=%s (%d)\n",
         (unsigned long)infoClassRaw, (void *)(uint64_t)userBuffer, (unsigned long)length,
         thread ? thread->ThreadId : 0U, KrGetStatusMessage(status), status);

    return KiEncodeCapabilitySyscallStatus(status);
}

static int64_t
KiHandleQuerySysinfo(EX_PROCESS *process, uint64_t infoClassRaw, uint64_t userBuffer, uint64_t length)
{
    KTHREAD *thread = KeGetCurrentThread();
    EX_SYSINFO_OVERVIEW overview = {0};
    HO_STATUS status;

    if (process == NULL)
        return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_INVALID_STATE);

    if (infoClassRaw != EX_SYSINFO_CLASS_OVERVIEW && infoClassRaw != EX_SYSINFO_CLASS_OVERVIEW_TEXT &&
        infoClassRaw != EX_SYSINFO_CLASS_THREAD_LIST && infoClassRaw != EX_SYSINFO_CLASS_THREAD_LIST_TEXT &&
        infoClassRaw != EX_SYSINFO_CLASS_MEMMAP_TEXT)
        return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_ILLEGAL_ARGUMENT);

    if (userBuffer == 0)
        return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_ILLEGAL_ARGUMENT);

    if (infoClassRaw == EX_SYSINFO_CLASS_OVERVIEW || infoClassRaw == EX_SYSINFO_CLASS_OVERVIEW_TEXT)
    {
        status = KiCaptureSysinfoOverview(&overview);
        if (status != EC_SUCCESS)
            return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

        if (infoClassRaw == EX_SYSINFO_CLASS_OVERVIEW)
        {
            if (length < sizeof(overview))
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_NOT_ENOUGH_MEMORY);

            status = KeUserBootstrapCopyOutBytes((HO_VIRTUAL_ADDRESS)userBuffer, &overview, sizeof(overview));
            if (status != EC_SUCCESS)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

            klog(KLOG_LEVEL_INFO,
                 KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_SUCCEEDED " class=%lu bytes=%lu thread=%u valid=%p\n",
                 (unsigned long)infoClassRaw, (unsigned long)sizeof(overview), thread ? thread->ThreadId : 0U,
                 (void *)(uint64_t)overview.ValidMask);

            return (int64_t)sizeof(overview);
        }

        {
            char text[EX_SYSINFO_TEXT_MAX_LENGTH];
            size_t textLength = 0;

            status = KiBuildSysinfoOverviewText(text, sizeof(text), &overview, &textLength);
            if (status != EC_SUCCESS)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

            if (length < textLength)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_NOT_ENOUGH_MEMORY);

            status = KeUserBootstrapCopyOutBytes((HO_VIRTUAL_ADDRESS)userBuffer, text, textLength);
            if (status != EC_SUCCESS)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

            klog(KLOG_LEVEL_INFO,
                 KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_SUCCEEDED " class=%lu bytes=%lu thread=%u valid=%p\n",
                 (unsigned long)infoClassRaw, (unsigned long)textLength, thread ? thread->ThreadId : 0U,
                 (void *)(uint64_t)overview.ValidMask);

            return (int64_t)textLength;
        }
    }

    if (infoClassRaw == EX_SYSINFO_CLASS_MEMMAP_TEXT)
    {
        char text[EX_SYSINFO_TEXT_MAX_LENGTH];
        size_t textLength = 0;

        status = KiBuildSysinfoMemmapText(text, sizeof(text), &textLength);
        if (status != EC_SUCCESS)
            return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

        if (length < textLength)
            return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_NOT_ENOUGH_MEMORY);

        status = KeUserBootstrapCopyOutBytes((HO_VIRTUAL_ADDRESS)userBuffer, text, textLength);
        if (status != EC_SUCCESS)
            return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

        klog(KLOG_LEVEL_INFO, KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_SUCCEEDED " class=%lu bytes=%lu thread=%u\n",
             (unsigned long)infoClassRaw, (unsigned long)textLength, thread ? thread->ThreadId : 0U);

        return (int64_t)textLength;
    }

    {
        EX_SYSINFO_THREAD_LIST threadList = {0};
        status = KiCaptureSysinfoThreadList(&threadList);
        if (status != EC_SUCCESS)
            return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

        if (infoClassRaw == EX_SYSINFO_CLASS_THREAD_LIST)
        {
            if (length < sizeof(threadList))
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_NOT_ENOUGH_MEMORY);

            status = KeUserBootstrapCopyOutBytes((HO_VIRTUAL_ADDRESS)userBuffer, &threadList, sizeof(threadList));
            if (status != EC_SUCCESS)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

            klog(KLOG_LEVEL_INFO,
                 KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_SUCCEEDED " class=%lu bytes=%lu thread=%u count=%u\n",
                 (unsigned long)infoClassRaw, (unsigned long)sizeof(threadList), thread ? thread->ThreadId : 0U,
                 threadList.ReturnedCount);

            return (int64_t)sizeof(threadList);
        }

        {
            char text[EX_SYSINFO_TEXT_MAX_LENGTH];
            size_t textLength = 0;

            status = KiBuildSysinfoThreadListText(text, sizeof(text), &threadList, &textLength);
            if (status != EC_SUCCESS)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

            if (length < textLength)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_NOT_ENOUGH_MEMORY);

            status = KeUserBootstrapCopyOutBytes((HO_VIRTUAL_ADDRESS)userBuffer, text, textLength);
            if (status != EC_SUCCESS)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

            klog(KLOG_LEVEL_INFO,
                 KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_SUCCEEDED " class=%lu bytes=%lu thread=%u count=%u\n",
                 (unsigned long)infoClassRaw, (unsigned long)textLength, thread ? thread->ThreadId : 0U,
                 threadList.ReturnedCount);

            return (int64_t)textLength;
        }
    }
}

static HO_NORETURN void
ExBootstrapEnterCallback(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    HO_STATUS status = ExBootstrapAdapterWrapThread(thread);
    if (status != EC_SUCCESS)
    {
        HO_KPANIC(status, "Failed to wrap bootstrap thread in Ex adapter");
    }

    process = ExBootstrapLookupRuntimeProcess(thread);
    if (process == NULL || !process->AddressSpace.Initialized)
    {
        HO_KPANIC(EC_INVALID_STATE, "Bootstrap enter: process root not initialized");
    }

    if (process->AddressSpace.RootPageTablePhys == 0)
    {
        HO_KPANIC(EC_INVALID_STATE, "Bootstrap enter: process root missing");
    }

    HO_PHYSICAL_ADDRESS activeRoot = 0;
    status = KeQueryActiveRootPageTable(&activeRoot);
    if (status != EC_SUCCESS)
    {
        HO_KPANIC(status, "Bootstrap enter: failed to query active root");
    }

    if (activeRoot != process->AddressSpace.RootPageTablePhys)
    {
        HO_KPANIC(EC_INVALID_STATE, "Bootstrap enter: dispatch root not installed");
    }

    KeUserBootstrapEnterCurrentThread();
}

static BOOL
ExBootstrapThreadOwnershipQueryCallback(const KTHREAD *thread)
{
    return ExBootstrapAdapterHasWrapper(thread);
}

static HO_STATUS
ExBootstrapThreadRootQueryCallback(const KTHREAD *thread, HO_PHYSICAL_ADDRESS *outRootPageTablePhys)
{
    const KE_KERNEL_ADDRESS_SPACE *kernelSpace = KeGetKernelAddressSpace();
    EX_THREAD *runtimeThread = NULL;

    if (thread == NULL || outRootPageTablePhys == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (kernelSpace == NULL || !kernelSpace->Initialized || kernelSpace->RootPageTablePhys == 0)
        return EC_INVALID_STATE;

    *outRootPageTablePhys = kernelSpace->RootPageTablePhys;

    runtimeThread = ExBootstrapLookupRuntimeThread(thread);
    if (runtimeThread != NULL)
    {
        EX_PROCESS *process = runtimeThread->Process;

        if (process == NULL || !process->AddressSpace.Initialized || process->AddressSpace.RootPageTablePhys == 0)
            return EC_INVALID_STATE;

        *outRootPageTablePhys = process->AddressSpace.RootPageTablePhys;
    }

    return EC_SUCCESS;
}

static HO_STATUS
ExBootstrapFinalizeCallback(KTHREAD *thread)
{
    return ExBootstrapAdapterFinalizeThread(thread);
}

static void
ExBootstrapTimerObserveCallback(KTHREAD *thread)
{
    (void)thread;
    KeUserBootstrapObserveCurrentThreadUserTimerPreemption();
}

static HO_NORETURN void
ExBootstrapUserExceptionCallback(KTHREAD *thread, const KE_BOOTSTRAP_USER_EXCEPTION_CONTEXT *context)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL || context == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Bootstrap user-fault handoff requires thread and context");

    HO_STATUS status = KiValidateBootstrapTerminationHandoff(thread, NULL, &process);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Bootstrap user-fault handoff validation failed");

    KiLogBootstrapUserExceptionEvidence(thread, process, context);

    status = ExBootstrapAdapterHandleExit(thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Bootstrap user-fault exit handoff validation failed");

    KeThreadExit();
}

HO_STATUS
ExBootstrapAdapterInit(void)
{
    return KeRegisterBootstrapCallbacks(ExBootstrapEnterCallback, ExBootstrapThreadOwnershipQueryCallback,
                                        ExBootstrapThreadRootQueryCallback, ExBootstrapFinalizeCallback,
                                        ExBootstrapTimerObserveCallback, ExBootstrapUserExceptionCallback);
}

HO_STATUS
ExBootstrapAdapterWrapThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    process = ExBootstrapLookupRuntimeProcess(thread);
    if (process == NULL)
        return EC_INVALID_STATE;

    if (process->Staging == NULL)
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

HO_STATUS
ExBootstrapAdapterFinalizeThread(KTHREAD *thread)
{
    EX_PROCESS *process = NULL;
    EX_THREAD *runtimeThread = NULL;

    runtimeThread = ExBootstrapLookupRuntimeThread(thread);
    if (runtimeThread == NULL)
        return EC_SUCCESS;

    process = runtimeThread->Process;

    HO_STATUS status = ExBootstrapTeardownProcessPayload(process);
    if (status == EC_SUCCESS)
    {
        klog(KLOG_LEVEL_INFO, KE_USER_BOOTSTRAP_LOG_TEARDOWN_COMPLETE " thread=%u\n", thread->ThreadId);
    }

    HO_STATUS releaseStatus = KiDestroyBootstrapWrapperObjects(thread);
    if (status == EC_SUCCESS)
        status = releaseStatus;

    return status;
}

BOOL
ExBootstrapAdapterHasWrapper(const KTHREAD *thread)
{
    return ExBootstrapLookupRuntimeThread(thread) != NULL;
}

struct KE_USER_BOOTSTRAP_STAGING *
ExBootstrapAdapterQueryThreadStaging(const KTHREAD *thread)
{
    EX_PROCESS *process = ExBootstrapLookupRuntimeProcess(thread);

    if (process == NULL)
        return NULL;

    return process->Staging;
}

HO_KERNEL_API HO_NODISCARD int64_t
ExBootstrapAdapterDispatchSyscall(uint64_t syscallNumber, uint64_t arg0, uint64_t arg1, uint64_t arg2)
{
    KTHREAD *thread = KeGetCurrentThread();
    EX_PROCESS *process = ExBootstrapLookupRuntimeProcess(thread);

    if (thread == NULL || process == NULL || process->Staging == NULL)
    {
        klog(KLOG_LEVEL_ERROR,
             KE_USER_BOOTSTRAP_LOG_INVALID_CAP_SYSCALL " nr=%lu thread=%u missing bootstrap staging\n",
             (unsigned long)syscallNumber, thread ? thread->ThreadId : 0U);
        return KiEncodeCapabilitySyscallStatus(EC_INVALID_STATE);
    }

    switch (syscallNumber)
    {
    case SYS_WRITE:
        return KiHandleCapabilityWrite(process, (EX_PRIVATE_HANDLE)arg0, arg1, arg2);
    case SYS_CLOSE:
        return KiHandleCapabilityClose(process, (EX_PRIVATE_HANDLE)arg0);
    case SYS_WAIT_ONE:
        return KiHandleCapabilityWaitOne(process, (EX_PRIVATE_HANDLE)arg0, arg1, arg2);
    case SYS_QUERY_SYSINFO:
        return KiHandleQuerySysinfo(process, arg0, arg1, arg2);
    default:
        klog(KLOG_LEVEL_WARNING, KE_USER_BOOTSTRAP_LOG_INVALID_CAP_SYSCALL " nr=%lu thread=%u args=(%p,%p,%p)\n",
             (unsigned long)syscallNumber, thread->ThreadId, (void *)(uint64_t)arg0, (void *)(uint64_t)arg1,
             (void *)(uint64_t)arg2);
        return KiEncodeCapabilitySyscallStatus(EC_NOT_SUPPORTED);
    }
}

HO_STATUS
ExBootstrapAdapterHandleExit(KTHREAD *thread)
{
    return KiValidateBootstrapTerminationHandoff(thread, NULL, NULL);
}

static HO_STATUS
KiDestroyBootstrapWrapperObjects(KTHREAD *thread)
{
    EX_THREAD *exThread = NULL;
    EX_PROCESS *process = NULL;
    HO_STATUS status = EC_SUCCESS;

    ExBootstrapUnpublishRuntimeAlias(thread, &exThread, &process);

    if (exThread != NULL && process == NULL)
        process = exThread->Process;

    if (process != NULL)
        status = ExBootstrapCloseAllPrivateHandles(process);

    if (exThread != NULL)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseThread(exThread);
        return status == EC_SUCCESS ? releaseStatus : status;
    }

    if (process != NULL)
    {
        HO_STATUS releaseStatus = ExBootstrapReleaseProcess(process);
        return status == EC_SUCCESS ? releaseStatus : status;
    }

    return status;
}

static HO_STATUS
KiValidateBootstrapTerminationHandoff(KTHREAD *thread, EX_THREAD **outRuntimeThread, EX_PROCESS **outProcess)
{
    EX_THREAD *runtimeThread = NULL;
    EX_PROCESS *process = NULL;
    KE_USER_BOOTSTRAP_LAYOUT layout = {0};

    if (outRuntimeThread != NULL)
        *outRuntimeThread = NULL;
    if (outProcess != NULL)
        *outProcess = NULL;

    if (thread == NULL)
        return EC_ILLEGAL_ARGUMENT;

    runtimeThread = ExBootstrapLookupRuntimeThread(thread);
    if (runtimeThread == NULL || runtimeThread->Thread != thread)
        return EC_INVALID_STATE;

    process = runtimeThread->Process;
    if (process == NULL || process->Staging == NULL || !process->AddressSpace.Initialized ||
        process->AddressSpace.RootPageTablePhys == 0)
    {
        return EC_INVALID_STATE;
    }

    HO_STATUS status = KeUserBootstrapQueryCurrentThreadLayout(&layout);
    if (status != EC_SUCCESS)
        return status;

    if (layout.OwnerRootPageTablePhys == 0 || layout.OwnerRootPageTablePhys != process->AddressSpace.RootPageTablePhys)
        return EC_INVALID_STATE;

    if (outRuntimeThread != NULL)
        *outRuntimeThread = runtimeThread;
    if (outProcess != NULL)
        *outProcess = process;

    return EC_SUCCESS;
}

static const char *
KiGetBootstrapUserExceptionShortName(uint8_t vectorNumber)
{
    switch (vectorNumber)
    {
    case 0U:
        return "#DE";
    case 14U:
        return "#PF";
    default:
        return "#??";
    }
}

static void
KiLogBootstrapPageFaultBits(uint32_t errorCode)
{
    klog(KLOG_LEVEL_ERROR, "[USERFAULT] PFERR: P=%u W=%u U=%u RSVD=%u I=%u PK=%u SS=%u SGX=%u\n", errorCode & 0x1U,
         (errorCode >> 1) & 0x1U, (errorCode >> 2) & 0x1U, (errorCode >> 3) & 0x1U, (errorCode >> 4) & 0x1U,
         (errorCode >> 5) & 0x1U, (errorCode >> 6) & 0x1U, (errorCode >> 15) & 0x1U);
}

static void
KiLogBootstrapUserExceptionEvidence(KTHREAD *thread,
                                    const EX_PROCESS *process,
                                    const KE_BOOTSTRAP_USER_EXCEPTION_CONTEXT *context)
{
    if (thread == NULL || process == NULL || context == NULL)
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Bootstrap user-fault evidence requires live runtime state");

    klog(KLOG_LEVEL_ERROR, "[USERFAULT] %s thread=%u program=%u rip=%p\n",
         KiGetBootstrapUserExceptionShortName(context->VectorNumber), thread->ThreadId, process->ProgramId,
         (void *)(uint64_t)context->InstructionPointer);

    if (context->HasFaultAddress)
    {
        klog(KLOG_LEVEL_ERROR, "[USERFAULT] CR2=%p PFERR=%p safe=%u\n", (void *)(uint64_t)context->FaultAddress,
             (void *)(uint64_t)context->PageFaultErrorCode, context->IsSafePageFaultContext);
        KiLogBootstrapPageFaultBits(context->PageFaultErrorCode);
    }
}
