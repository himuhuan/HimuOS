/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo.c
 * Description:
 * Ke Layer - System information dispatch facade.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/sysinfo.h>
#include <kernel/hodefs.h>
#include "sysinfo_internal.h"

// ─────────────────────────────────────────────────────────────
// Main API
// ─────────────────────────────────────────────────────────────

HO_STATUS HO_KERNEL_API
KeQuerySystemInformation(KE_SYSINFO_CLASS Class, void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    if (Class >= KE_SYSINFO_MAX)
        return EC_ILLEGAL_ARGUMENT;

    switch (Class)
    {
    case KE_SYSINFO_BOOT_MEMORY_MAP:
#if __HO_DEBUG_BUILD__
        return QueryBootMemoryMap(Buffer, BufferSize, RequiredSize);
#else
        return EC_NOT_SUPPORTED;
#endif

    case KE_SYSINFO_CPU_BASIC:
        return QueryCpuBasic(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_CPU_FEATURES:
        return QueryCpuFeatures(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_PAGE_TABLE:
        return QueryPageTable(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_PHYSICAL_MEM_STATS:
        return QueryPhysicalMemStats(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_VIRTUAL_LAYOUT:
        return QueryVirtualLayout(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_GDT:
        return QueryGdt(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_TSS:
        return QueryTss(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_IDT:
        return QueryIdt(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_TIME_SOURCE:
        return QueryTimeSource(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_UPTIME:
        return QueryUptime(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_SYSTEM_VERSION:
        return QuerySystemVersion(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_CLOCK_EVENT:
        return QueryClockEvent(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_SCHEDULER:
        return QueryScheduler(Buffer, BufferSize, RequiredSize);

    default:
        return EC_ILLEGAL_ARGUMENT;
    }
}
