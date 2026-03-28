/**
 * HimuOperatingSystem
 *
 * File: boot_mapping_manifest.h
 * Description:
 * Shared boot mapping manifest definitions used by both the EFI loader and the kernel.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

#define BOOT_MAPPING_MANIFEST_ALIGNMENT 8U

#define BOOT_MAPPING_FOURCC(a, b, c, d)                                                                       \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8) | ((uint32_t)(uint8_t)(c) << 16) |               \
     ((uint32_t)(uint8_t)(d) << 24))

#define BOOT_MAPPING_MANIFEST_MAGIC   BOOT_MAPPING_FOURCC('B', 'M', 'M', 'F')
#define BOOT_MAPPING_MANIFEST_VERSION 1U

typedef enum BOOT_MAPPING_REGION_TYPE
{
    BOOT_MAPPING_REGION_INVALID = 0,
    BOOT_MAPPING_REGION_IDENTITY,
    BOOT_MAPPING_REGION_HHDM,
    BOOT_MAPPING_REGION_BOOT_STAGING,
    BOOT_MAPPING_REGION_BOOT_HANDOFF,
    BOOT_MAPPING_REGION_BOOT_PAGE_TABLES,
    BOOT_MAPPING_REGION_ACPI_RSDP,
    BOOT_MAPPING_REGION_ACPI_ROOT,
    BOOT_MAPPING_REGION_ACPI_TABLE,
    BOOT_MAPPING_REGION_KERNEL_CODE,
    BOOT_MAPPING_REGION_KERNEL_DATA,
    BOOT_MAPPING_REGION_KERNEL_STACK,
    BOOT_MAPPING_REGION_KERNEL_IST_STACK,
    BOOT_MAPPING_REGION_FRAMEBUFFER,
    BOOT_MAPPING_REGION_HPET_MMIO,
    BOOT_MAPPING_REGION_LAPIC_MMIO,
    BOOT_MAPPING_REGION_MAX,
} BOOT_MAPPING_REGION_TYPE;

typedef enum BOOT_MAPPING_PROVENANCE
{
    BOOT_MAPPING_PROVENANCE_NONE = 0,
    BOOT_MAPPING_PROVENANCE_STATIC,
    BOOT_MAPPING_PROVENANCE_EFI_MEMORY_TYPE,
    BOOT_MAPPING_PROVENANCE_ACPI_SIGNATURE,
    BOOT_MAPPING_PROVENANCE_CPU_MSR,
    BOOT_MAPPING_PROVENANCE_MAX,
} BOOT_MAPPING_PROVENANCE;

typedef enum BOOT_MAPPING_LIFETIME
{
    BOOT_MAPPING_LIFETIME_INVALID = 0,
    BOOT_MAPPING_LIFETIME_TEMPORARY,
    BOOT_MAPPING_LIFETIME_RETAINED,
    BOOT_MAPPING_LIFETIME_KERNEL,
    BOOT_MAPPING_LIFETIME_FIRMWARE,
    BOOT_MAPPING_LIFETIME_DEVICE,
    BOOT_MAPPING_LIFETIME_MAX,
} BOOT_MAPPING_LIFETIME;

typedef enum BOOT_MAPPING_GRANULARITY
{
    BOOT_MAPPING_GRANULARITY_INVALID = 0,
    BOOT_MAPPING_GRANULARITY_4KB,
    BOOT_MAPPING_GRANULARITY_2MB,
    BOOT_MAPPING_GRANULARITY_1GB,
    BOOT_MAPPING_GRANULARITY_MIXED,
    BOOT_MAPPING_GRANULARITY_MAX,
} BOOT_MAPPING_GRANULARITY;

typedef struct BOOT_MAPPING_MANIFEST_HEADER
{
    uint32_t Magic;
    uint16_t Version;
    uint16_t HeaderSize;
    uint32_t TotalSize;
    uint32_t EntrySize;
    uint32_t EntryCount;
    uint32_t EntryCapacity;
    uint32_t Reserved;
} BOOT_MAPPING_MANIFEST_HEADER;

typedef struct BOOT_MAPPING_MANIFEST_ENTRY
{
    HO_VIRTUAL_ADDRESS VirtualStart;
    HO_PHYSICAL_ADDRESS PhysicalStart;
    uint64_t Size;
    uint64_t Attributes;
    uint32_t ProvenanceValue;
    uint16_t Type;
    uint8_t Provenance;
    uint8_t Lifetime;
    uint8_t Granularity;
    uint8_t Reserved[7];
} BOOT_MAPPING_MANIFEST_ENTRY;

static inline uint32_t
BootMappingManifestHeaderSize(void)
{
    return (uint32_t)HO_ALIGN_UP(sizeof(BOOT_MAPPING_MANIFEST_HEADER), BOOT_MAPPING_MANIFEST_ALIGNMENT);
}

static inline uint32_t
BootMappingManifestTotalSizeForCapacity(uint32_t entryCapacity)
{
    uint64_t totalSize = (uint64_t)BootMappingManifestHeaderSize() +
                         (uint64_t)entryCapacity * sizeof(BOOT_MAPPING_MANIFEST_ENTRY);
    return (uint32_t)HO_ALIGN_UP(totalSize, BOOT_MAPPING_MANIFEST_ALIGNMENT);
}

static inline BOOT_MAPPING_MANIFEST_ENTRY *
BootMappingManifestEntries(BOOT_MAPPING_MANIFEST_HEADER *manifest)
{
    if (!manifest)
        return NULL;

    return (BOOT_MAPPING_MANIFEST_ENTRY *)((uint8_t *)manifest + manifest->HeaderSize);
}

static inline const BOOT_MAPPING_MANIFEST_ENTRY *
BootMappingManifestConstEntries(const BOOT_MAPPING_MANIFEST_HEADER *manifest)
{
    if (!manifest)
        return NULL;

    return (const BOOT_MAPPING_MANIFEST_ENTRY *)((const uint8_t *)manifest + manifest->HeaderSize);
}

static inline uint32_t
BootMappingManifestUsedSize(const BOOT_MAPPING_MANIFEST_HEADER *manifest)
{
    if (!manifest)
        return 0;

    return manifest->HeaderSize + manifest->EntryCount * manifest->EntrySize;
}