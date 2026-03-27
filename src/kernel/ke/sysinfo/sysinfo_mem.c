/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo_mem.c
 * Description: Memory, page table, virtual layout, and system version queries.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"
#include <kernel/ke/sysinfo.h>
#include <kernel/hodefs.h>
#include <kernel/init.h>
#include <libc/string.h>

static inline uint64_t
ReadCr3(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

// ─────────────────────────────────────────────────────────────
// Memory map & page table queries
// ─────────────────────────────────────────────────────────────

#if __HO_DEBUG_BUILD__
HO_STATUS
QueryBootMemoryMap(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    BOOT_CAPSULE *capsule = KeGetBootCapsule();
    if (!capsule)
        return EC_INVALID_STATE;

    size_t required = capsule->Layout.MemoryMapSize;

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    HO_VIRTUAL_ADDRESS mapVirt = HHDM_BASE_VA + capsule->MemoryMapPhys;
    memcpy(Buffer, (void *)mapVirt, required);

    return EC_SUCCESS;
}
#endif

HO_STATUS
QueryPageTable(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_PAGE_TABLE);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_PAGE_TABLE *info = (SYSINFO_PAGE_TABLE *)Buffer;
    info->Cr3 = ReadCr3();

    return EC_SUCCESS;
}

HO_STATUS
QueryPhysicalMemStats(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    (void)Buffer;
    (void)BufferSize;
    (void)RequiredSize;
    return EC_NOT_SUPPORTED;
}

HO_STATUS
QueryVirtualLayout(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_VIRTUAL_LAYOUT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_VIRTUAL_LAYOUT *info = (SYSINFO_VIRTUAL_LAYOUT *)Buffer;
    info->KernelBase = KRNL_BASE_VA;
    info->KernelStack = KRNL_STACK_VA;
    info->HhdmBase = HHDM_BASE_VA;
    info->MmioBase = MMIO_BASE_VA;

    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// System version
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
