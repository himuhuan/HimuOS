/**
 * HimuOperatingSystem
 *
 * File: blmm.h
 * Description: Bootloader memory management definitions.
 * Only for AMD64 architecture.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "arch/amd64/efi_mem.h"
#include "boot/boot_capsule.h"
#include "ho_balloc.h"

EFI_MEMORY_MAP *GetLoaderRuntimeMemoryMap();
EFI_STATUS LoadMemoryMap(HO_PHYSICAL_ADDRESS mapBasePhys, UINT64 maxSize, OUT UINTN *memoryMap);

UINT64 GetCapsulePhysPages(const BOOT_CAPSULE_LAYOUT *block, BOOT_CAPSULE_PAGE_LAYOUT *pageLayout);
BOOT_CAPSULE *CreateCapsule(const BOOT_CAPSULE_LAYOUT *layout);

UINT64 CreateInitialMapping(BOOT_CAPSULE *capsule);

typedef struct
{
    BOOL NotPresent;
    UINT64 TableBasePhys;
    HOB_BALLOC *BumpAllocator;
    UINT64 AddrPhys;
    UINT64 AddrVirt;
    UINT64 Flags;
    UINT64 PageSize;
} MAP_PAGE_PARAMS;
EFI_STATUS MapPage(MAP_PAGE_PARAMS *request);
EFI_STATUS
MapRegion(HOB_BALLOC *allocator, UINT64 pml4BasePhys, UINT64 physStart, UINT64 virtStart, UINT64 size, UINT64 flags);

