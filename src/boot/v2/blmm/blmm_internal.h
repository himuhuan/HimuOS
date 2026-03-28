/**
 * HimuOperatingSystem
 *
 * File: blmm/blmm_internal.h
 * Description: Private declarations shared by bootloader memory-management modules.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "../blmm.h"
#include "../bootloader.h"
#include "arch/amd64/pm.h"
#include "arch/amd64/asm.h"
#include "arch/amd64/acpi.h"
#include "../ho_balloc.h"
#include "../io.h"

typedef struct BOOT_MAPPING_RECORD_PARAMS
{
    HO_VIRTUAL_ADDRESS VirtualStart;
    HO_PHYSICAL_ADDRESS PhysicalStart;
    uint64_t Size;
    uint64_t Attributes;
    uint32_t ProvenanceValue;
    BOOT_MAPPING_REGION_TYPE Type;
    BOOT_MAPPING_PROVENANCE Provenance;
    BOOT_MAPPING_LIFETIME Lifetime;
    BOOT_MAPPING_GRANULARITY Granularity;
} BOOT_MAPPING_RECORD_PARAMS;

EFI_STATUS MapFullHhdmFromMemoryMap(HOB_BALLOC *allocator,
                                    UINT64 pml4BasePhys,
                                    BOOT_CAPSULE *capsule,
                                    EFI_MEMORY_MAP *memoryMap,
                                    UINT64 nxFlag,
                                    UINT64 *mappedDescCount,
                                    UINT64 *highestPhysExclusive);
EFI_STATUS MapAcpiTables(HOB_BALLOC *allocator,
                         UINT64 pml4BasePhys,
                         BOOT_CAPSULE *capsule,
                         HO_PHYSICAL_ADDRESS rsdpPhys);
EFI_STATUS BootMappingManifestInitialize(BOOT_CAPSULE *capsule);
EFI_STATUS BootMappingManifestRecord(BOOT_CAPSULE *capsule, const BOOT_MAPPING_RECORD_PARAMS *params);
BOOT_MAPPING_GRANULARITY DetectBootMappingGranularity(HO_PHYSICAL_ADDRESS physStart,
                                                      HO_VIRTUAL_ADDRESS virtStart,
                                                      uint64_t size);
void DumpBootMappingManifestSummary(const BOOT_CAPSULE *capsule);
UINT64 GetNxFlag(void);
UINT64 GetHhdmMapFlags(EFI_MEMORY_TYPE type, UINT64 nxFlag);
BOOL IsSameMapping(UINT64 existingEntry, UINT64 targetPhys, UINT64 flags, UINT64 isPresent);
