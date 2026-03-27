/**
 * HimuOperatingSystem
 *
 * File: blmm_internal.h
 * Description: Internal declarations for bootloader memory management modules.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "blmm.h"
#include "boot/v2/bootloader.h"
#include "arch/amd64/pm.h"
#include "arch/amd64/asm.h"
#include "arch/amd64/acpi.h"
#include "boot/v2/ho_balloc.h"
#include "boot/v2/io.h"

// ─────────────────────────────────────────────────────────────
// Manifest helpers (blmm_manifest.c)
// ─────────────────────────────────────────────────────────────

BOOT_MAPPING_MANIFEST_ENTRY *BlGetBootMappingManifestEntries(BOOT_CAPSULE *capsule);
void BlCopyBootMappingManifestEntry(BOOT_MAPPING_MANIFEST_ENTRY *dst, const BOOT_MAPPING_MANIFEST_ENTRY *src);
UINT32 BlGetBootMappingCacheType(UINT64 flags);
UINT64 BlGetBootMappingEntryAttributes(UINT64 flags, UINT64 extraAttributes);
BOOL BlIsBootMappingUmbrellaCategory(UINT32 category);
EFI_STATUS BlAppendBootMappingEntry(BOOT_CAPSULE *capsule, const BOOT_MAPPING_MANIFEST_ENTRY *entry);
void BlSortBootMappingManifest(BOOT_CAPSULE *capsule);
EFI_STATUS BlValidateBootMappingManifest(BOOT_CAPSULE *capsule);

// ─────────────────────────────────────────────────────────────
// HHDM / ACPI mapping (blmm_hhdm_acpi.c)
// ─────────────────────────────────────────────────────────────

UINT64 BlGetNxFlag(void);
UINT64 BlGetHhdmMapFlags(EFI_MEMORY_TYPE type, UINT64 nxFlag);
EFI_STATUS BlMapFullHhdmFromMemoryMap(HOB_BALLOC *allocator,
                                      UINT64 pml4BasePhys,
                                      BOOT_CAPSULE *capsule,
                                      EFI_MEMORY_MAP *memoryMap,
                                      UINT64 nxFlag,
                                      UINT64 *mappedDescCount,
                                      UINT64 *highestPhysExclusive);
EFI_STATUS BlMapAcpiTables(HOB_BALLOC *allocator,
                           UINT64 pml4BasePhys,
                           BOOT_CAPSULE *capsule,
                           HO_PHYSICAL_ADDRESS rsdpPhys);

// ─────────────────────────────────────────────────────────────
// Paging helpers (blmm_paging.c)
// ─────────────────────────────────────────────────────────────

EFI_STATUS BlMapInitialIdentityWindow(HOB_BALLOC *allocator,
                                      UINT64 pml4BasePhys,
                                      BOOT_CAPSULE *capsule,
                                      UINT64 size,
                                      UINT64 flags);
BOOL BlIsSameMapping(UINT64 existingEntry, UINT64 targetPhys, UINT64 flags, UINT64 isPresent);
