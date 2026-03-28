/**
 * HimuOperatingSystem
 *
 * File: blmm/hhdm.c
 * Description: HHDM mapping helpers for the EFI bootloader.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "blmm_internal.h"

EFI_STATUS
MapFullHhdmFromMemoryMap(HOB_BALLOC *allocator,
                         UINT64 pml4BasePhys,
                         EFI_MEMORY_MAP *memoryMap,
                         UINT64 nxFlag,
                         UINT64 *mappedDescCount,
                         UINT64 *highestPhysExclusive)
{
    if (!allocator || !memoryMap || !mappedDescCount || !highestPhysExclusive)
        return EFI_INVALID_PARAMETER;

    if (memoryMap->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) || memoryMap->DescriptorSize == 0)
        return EFI_INVALID_PARAMETER;

    *mappedDescCount = 0;
    *highestPhysExclusive = 0;

    UINT64 descCount = memoryMap->DescriptorTotalSize / memoryMap->DescriptorSize;
    for (UINT64 idx = 0; idx < descCount; ++idx)
    {
        UINT8 *descAddr = (UINT8 *)memoryMap->Segs + idx * memoryMap->DescriptorSize;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)descAddr;

        if (!desc || desc->NumberOfPages == 0)
            continue;

        UINT64 mapSize = desc->NumberOfPages << PAGE_SHIFT;
        UINT64 mapPhysStart = desc->PhysicalStart;
        UINT64 mapPhysEndExclusive = mapPhysStart + mapSize;
        if (mapPhysEndExclusive < mapPhysStart)
            return EFI_INVALID_PARAMETER;

        UINT64 flags = GetHhdmMapFlags((EFI_MEMORY_TYPE)desc->Type, nxFlag);
        EFI_STATUS status =
            MapRegion(allocator, pml4BasePhys, mapPhysStart, HHDM_BASE_VA + mapPhysStart, mapSize, flags);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Map FULL HHDM failed at descriptor=%u type=%u phys=%p pages=%u: %k\r\n", idx, desc->Type,
                      (void *)(UINTN)mapPhysStart, desc->NumberOfPages, status);
            return status;
        }

        if (mapPhysEndExclusive > *highestPhysExclusive)
            *highestPhysExclusive = mapPhysEndExclusive;
        (*mappedDescCount)++;
    }

    return EFI_SUCCESS;
}
