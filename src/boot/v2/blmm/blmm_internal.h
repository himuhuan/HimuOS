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

EFI_STATUS MapFullHhdmFromMemoryMap(HOB_BALLOC *allocator,
                                    UINT64 pml4BasePhys,
                                    EFI_MEMORY_MAP *memoryMap,
                                    UINT64 nxFlag,
                                    UINT64 *mappedDescCount,
                                    UINT64 *highestPhysExclusive);
EFI_STATUS MapAcpiTables(HOB_BALLOC *allocator, UINT64 pml4BasePhys, HO_PHYSICAL_ADDRESS rsdpPhys);
UINT64 GetNxFlag(void);
UINT64 GetHhdmMapFlags(EFI_MEMORY_TYPE type, UINT64 nxFlag);
BOOL IsSameMapping(UINT64 existingEntry, UINT64 targetPhys, UINT64 flags, UINT64 isPresent);
