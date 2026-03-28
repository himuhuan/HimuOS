/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo/sysinfo.c
 * Description:
 * Ke Layer - System information query API implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"

// ─────────────────────────────────────────────────────────────
// Query Handlers
// ─────────────────────────────────────────────────────────────

HO_STATUS
QuerySystemVersion(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_SYSTEM_VERSION);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_SYSTEM_VERSION *info = (SYSINFO_SYSTEM_VERSION *)Buffer;
    memset(info, 0, sizeof(*info));

    info->Major = 1;
    info->Minor = 0;
    info->Patch = 0;

    // __DATE__ format: "Mmm dd yyyy" (12 chars with null)
    // __TIME__ format: "hh:mm:ss" (9 chars with null)
    const char *buildDate = __DATE__;
    const char *buildTime = __TIME__;

    size_t dateLen = strlen(buildDate);
    if (dateLen >= sizeof(info->BuildDate))
        dateLen = sizeof(info->BuildDate) - 1;
    memcpy(info->BuildDate, buildDate, dateLen);
    info->BuildDate[dateLen] = '\0';

    size_t timeLen = strlen(buildTime);
    if (timeLen >= sizeof(info->BuildTime))
        timeLen = sizeof(info->BuildTime) - 1;
    memcpy(info->BuildTime, buildTime, timeLen);
    info->BuildTime[timeLen] = '\0';

    return EC_SUCCESS;
}

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
