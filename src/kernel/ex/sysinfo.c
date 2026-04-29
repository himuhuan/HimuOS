/**
 * HimuOperatingSystem
 *
 * File: ex/sysinfo.c
 * Description: Ex-facing bootstrap sysinfo capture and text rendering.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ex_bootstrap_internal.h"

#include <kernel/ke/kthread.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/sysinfo.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

static int64_t
KiEncodeCapabilitySyscallStatus(HO_STATUS status)
{
    return status == EC_SUCCESS ? 0 : -(int64_t)status;
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

static const char *
KiGetSysinfoProcessStateName(uint32_t state)
{
    switch (state)
    {
    case EX_SYSINFO_PROCESS_STATE_CREATED:
        return "CREATED";
    case EX_SYSINFO_PROCESS_STATE_READY:
        return "READY";
    case EX_SYSINFO_PROCESS_STATE_RUNNING:
        return "RUNNING";
    case EX_SYSINFO_PROCESS_STATE_BLOCKED:
        return "BLOCKED";
    case EX_SYSINFO_PROCESS_STATE_SLEEPING:
        return "SLEEPING";
    case EX_SYSINFO_PROCESS_STATE_TERMINATED:
        return "TERMINATED";
    default:
        return "UNKNOWN";
    }
}

static HO_STATUS
KiBuildSysinfoProcessListText(char *buffer,
                              size_t capacity,
                              const EX_SYSINFO_PROCESS_LIST *processList,
                              size_t *outLength)
{
    size_t length = 0;

    if (buffer == NULL || processList == NULL || outLength == NULL || capacity == 0)
        return EC_ILLEGAL_ARGUMENT;

    if (!KiAppendSysinfoLiteral(buffer, &length, capacity, "PID  STATE       PPID  TID  NAME\n") ||
        !KiAppendSysinfoLiteral(buffer, &length, capacity, "---  ----------  ----  ---  ----\n"))
    {
        return EC_NOT_ENOUGH_MEMORY;
    }

    for (uint32_t index = 0; index < processList->ReturnedCount; ++index)
    {
        const EX_SYSINFO_PROCESS_ENTRY *entry = &processList->Entries[index];
        const char *stateName = KiGetSysinfoProcessStateName(entry->State);

        if (!KiAppendSysinfoPaddedUInt64(buffer, &length, capacity, entry->ProcessId, 3U) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  ") ||
            !KiAppendSysinfoPaddedLiteral(buffer, &length, capacity, stateName, 10U) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  ") ||
            !KiAppendSysinfoPaddedUInt64(buffer, &length, capacity, entry->ParentProcessId, 4U) ||
            !KiAppendSysinfoLiteral(buffer, &length, capacity, "  ") ||
            !KiAppendSysinfoPaddedUInt64(buffer, &length, capacity, entry->MainThreadId, 3U) ||
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

int64_t
ExBootstrapHandleQuerySysinfo(EX_PROCESS *process, uint64_t infoClassRaw, uint64_t userBuffer, uint64_t length)
{
    KTHREAD *thread = KeGetCurrentThread();
    EX_SYSINFO_OVERVIEW overview = {0};
    HO_STATUS status;

    if (process == NULL)
        return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_INVALID_STATE);

    if (infoClassRaw != EX_SYSINFO_CLASS_OVERVIEW && infoClassRaw != EX_SYSINFO_CLASS_OVERVIEW_TEXT &&
        infoClassRaw != EX_SYSINFO_CLASS_THREAD_LIST && infoClassRaw != EX_SYSINFO_CLASS_THREAD_LIST_TEXT &&
        infoClassRaw != EX_SYSINFO_CLASS_MEMMAP_TEXT && infoClassRaw != EX_SYSINFO_CLASS_PROCESS_LIST &&
        infoClassRaw != EX_SYSINFO_CLASS_PROCESS_LIST_TEXT)
    {
        return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_ILLEGAL_ARGUMENT);
    }

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

    if (infoClassRaw == EX_SYSINFO_CLASS_PROCESS_LIST || infoClassRaw == EX_SYSINFO_CLASS_PROCESS_LIST_TEXT)
    {
        EX_SYSINFO_PROCESS_LIST processList = {0};
        status = ExBootstrapCaptureProcessList(&processList);
        if (status != EC_SUCCESS)
            return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

        if (infoClassRaw == EX_SYSINFO_CLASS_PROCESS_LIST)
        {
            if (length < sizeof(processList))
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, EC_NOT_ENOUGH_MEMORY);

            status = KeUserBootstrapCopyOutBytes((HO_VIRTUAL_ADDRESS)userBuffer, &processList, sizeof(processList));
            if (status != EC_SUCCESS)
                return KiRejectQuerySysinfo(infoClassRaw, userBuffer, length, status);

            klog(KLOG_LEVEL_INFO,
                 KE_USER_BOOTSTRAP_LOG_QUERY_SYSINFO_SUCCEEDED " class=%lu bytes=%lu thread=%u count=%u\n",
                 (unsigned long)infoClassRaw, (unsigned long)sizeof(processList), thread ? thread->ThreadId : 0U,
                 processList.ReturnedCount);

            return (int64_t)sizeof(processList);
        }

        {
            char text[EX_SYSINFO_TEXT_MAX_LENGTH];
            size_t textLength = 0;

            status = KiBuildSysinfoProcessListText(text, sizeof(text), &processList, &textLength);
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
                 processList.ReturnedCount);

            return (int64_t)textLength;
        }
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
