/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo/memory.c
 * Description:
 * Memory-oriented system information query handlers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"

static void
KiFillVmmArenaOverview(SYSINFO_VMM_ARENA_OVERVIEW *outArena, const KE_KVA_ARENA_INFO *arenaInfo)
{
    outArena->TotalPages = arenaInfo->TotalPages;
    outArena->FreePages = arenaInfo->FreePages;
    outArena->ActiveAllocations = arenaInfo->ActiveAllocations;
}

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
    const size_t required = sizeof(SYSINFO_PHYSICAL_MEM_STATS);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    KE_PMM_STATS pmmStats;
    HO_STATUS status = KePmmQueryStats(&pmmStats);
    if (status != EC_SUCCESS)
        return status;

    SYSINFO_PHYSICAL_MEM_STATS *info = (SYSINFO_PHYSICAL_MEM_STATS *)Buffer;
    info->TotalBytes = pmmStats.TotalBytes;
    info->FreeBytes = pmmStats.FreeBytes;
    info->AllocatedBytes = pmmStats.AllocatedBytes;
    info->ReservedBytes = pmmStats.ReservedBytes;
    return EC_SUCCESS;
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

HO_STATUS
QueryVmmOverview(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_VMM_OVERVIEW);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    const KE_KERNEL_ADDRESS_SPACE *space = KeGetKernelAddressSpace();
    if (!space || !space->Initialized)
        return EC_INVALID_STATE;

    KE_KVA_ARENA_INFO stackArena;
    HO_STATUS status = KeKvaQueryArenaInfo(KE_KVA_ARENA_STACK, &stackArena);
    if (status != EC_SUCCESS)
        return status;

    KE_KVA_ARENA_INFO fixmapArena;
    status = KeKvaQueryArenaInfo(KE_KVA_ARENA_FIXMAP, &fixmapArena);
    if (status != EC_SUCCESS)
        return status;

    KE_KVA_ARENA_INFO heapArena;
    status = KeKvaQueryArenaInfo(KE_KVA_ARENA_HEAP, &heapArena);
    if (status != EC_SUCCESS)
        return status;

    KE_KVA_USAGE_INFO usageInfo;
    status = KeKvaQueryUsageInfo(&usageInfo);
    if (status != EC_SUCCESS)
        return status;

    SYSINFO_VMM_OVERVIEW *info = (SYSINFO_VMM_OVERVIEW *)Buffer;
    memset(info, 0, sizeof(*info));
    info->ImportedRegionCount = space->RegionCount;
    KiFillVmmArenaOverview(&info->StackArena, &stackArena);
    KiFillVmmArenaOverview(&info->FixmapArena, &fixmapArena);
    KiFillVmmArenaOverview(&info->HeapArena, &heapArena);
    info->ActiveKvaRangeCount = usageInfo.ActiveRangeCount;
    info->FixmapTotalSlots = usageInfo.FixmapTotalSlots;
    info->FixmapActiveSlots = usageInfo.FixmapActiveSlots;
    return EC_SUCCESS;
}

HO_STATUS
QueryActiveKvaRanges(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_ACTIVE_KVA_RANGES);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    KE_KVA_ACTIVE_RANGE_SNAPSHOT snapshot;
    HO_STATUS status = KeKvaQueryActiveRanges(&snapshot);
    if (status != EC_SUCCESS)
        return status;

    SYSINFO_ACTIVE_KVA_RANGES *info = (SYSINFO_ACTIVE_KVA_RANGES *)Buffer;
    memset(info, 0, sizeof(*info));
    info->TotalActiveRangeCount = snapshot.TotalActiveRangeCount;
    info->ReturnedRangeCount = snapshot.ReturnedRangeCount;
    info->Truncated = snapshot.Truncated;

    for (uint32_t idx = 0; idx < snapshot.ReturnedRangeCount; ++idx)
    {
        info->Ranges[idx].Arena = snapshot.Ranges[idx].Arena;
        info->Ranges[idx].RecordId = snapshot.Ranges[idx].RecordId;
        info->Ranges[idx].Generation = snapshot.Ranges[idx].Generation;
        info->Ranges[idx].BaseAddress = snapshot.Ranges[idx].BaseAddress;
        info->Ranges[idx].EndAddressExclusive = snapshot.Ranges[idx].EndAddressExclusive;
        info->Ranges[idx].UsableBase = snapshot.Ranges[idx].UsableBase;
        info->Ranges[idx].UsableEndExclusive = snapshot.Ranges[idx].UsableEndExclusive;
        info->Ranges[idx].TotalPages = snapshot.Ranges[idx].TotalPages;
        info->Ranges[idx].UsablePages = snapshot.Ranges[idx].UsablePages;
        info->Ranges[idx].GuardLowerPages = snapshot.Ranges[idx].GuardLowerPages;
        info->Ranges[idx].GuardUpperPages = snapshot.Ranges[idx].GuardUpperPages;
    }

    return EC_SUCCESS;
}
