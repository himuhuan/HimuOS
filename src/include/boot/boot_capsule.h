/**
 * HimuOperatingSystem
 *
 * File: boot_capsule.h
 * Description:
 * Boot capsule containing information passed from the bootloader to the kernel.
 *
 * Only for AMD64 architecture.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "arch/amd64/efi_min.h"
#include "arch/amd64/pm.h"

#define BOOT_FRAMEBUFFER_VA     MMIO_BASE_VA
#define BOOT_KRNL_ENTRY_VA      KRNL_ENTRY_VA
#define BOOT_KRNL_STACK_VA      KRNL_STACK_VA      // Kernel stack BOTTOM virtual address
#define BOOT_KRNL_IST1_STACK_VA KRNL_IST1_STACK_VA // Kernel IST1 stack BOTTOM virtual address
#define BOOT_HHDM_BASE_VA       HHDM_BASE_VA

#define BOOT_CAPSULE_MAGIC      0x214F5348 // 'HOS!'
#define BOOT_MAPPING_MANIFEST_MAGIC   0x314D4D42U // 'BMM1'
#define BOOT_MAPPING_MANIFEST_VERSION 1U

typedef struct BOOT_CAPSULE_LAYOUT
{
    size_t HeaderSize;    // Size of the BOOT_CAPSULE structure itself.
    size_t MemoryMapSize; // Size of the memory map
    size_t KrnlCodeSize;  // Size of the kernel physical memory occupied by the kernel ELF code segments
    size_t KrnlDataSize;  // Size of the kernel physical memory occupied by the kernel ELF data segments (BSS)
    size_t KrnlStackSize; // Size of the kernel stack
    size_t IST1StackSize; // Size of the kernel IST#1 stack
} BOOT_CAPSULE_LAYOUT;

typedef struct BOOT_CAPSULE_PAGE_LAYOUT
{
    UINT64 HeaderWithMapPages; // Pages for BOOT_CAPSULE header plus memory map (aligned to 4KB)
    UINT64 KrnlPages;          // Pages for kernel code + data (aligned to 4KB)
    UINT64 KrnlStackPages;     // Pages for kernel stack (aligned to 4KB)
    UINT64 IST1StackPages;     // Pages for IST#1 stack (aligned to 4KB)
    UINT64 TotalPages;         // Total pages required for the capsule (sum of above)
} BOOT_CAPSULE_PAGE_LAYOUT;

typedef enum BOOT_MAPPING_CATEGORY
{
    BOOT_MAPPING_CATEGORY_NONE = 0,
    BOOT_MAPPING_CATEGORY_BOOTSTRAP_IDENTITY = 1,
    BOOT_MAPPING_CATEGORY_HHDM = 2,
    BOOT_MAPPING_CATEGORY_BOOT_CAPSULE = 3,
    BOOT_MAPPING_CATEGORY_ACPI = 4,
    BOOT_MAPPING_CATEGORY_KERNEL_TEXT = 5,
    BOOT_MAPPING_CATEGORY_KERNEL_DATA = 6,
    BOOT_MAPPING_CATEGORY_BOOT_STACK = 7,
    BOOT_MAPPING_CATEGORY_IST_STACK = 8,
    BOOT_MAPPING_CATEGORY_FRAMEBUFFER = 9,
    BOOT_MAPPING_CATEGORY_PAGE_TABLES = 10,
    BOOT_MAPPING_CATEGORY_LAPIC_MMIO = 11,
    BOOT_MAPPING_CATEGORY_HPET_MMIO = 12,
    BOOT_MAPPING_CATEGORY_MAX
} BOOT_MAPPING_CATEGORY;

typedef enum BOOT_MAPPING_CACHE_TYPE
{
    BOOT_MAPPING_CACHE_DEFAULT = 0,
    BOOT_MAPPING_CACHE_WRITE_THROUGH = 1,
    BOOT_MAPPING_CACHE_UNCACHEABLE = 2,
} BOOT_MAPPING_CACHE_TYPE;

#define BOOT_MAPPING_ATTR_PRESENT        (1ULL << 0)
#define BOOT_MAPPING_ATTR_READ           (1ULL << 1)
#define BOOT_MAPPING_ATTR_WRITE          (1ULL << 2)
#define BOOT_MAPPING_ATTR_EXECUTE        (1ULL << 3)
#define BOOT_MAPPING_ATTR_USER           (1ULL << 4)
#define BOOT_MAPPING_ATTR_BOOT_IMPORTED  (1ULL << 5)
#define BOOT_MAPPING_ATTR_RELEASABLE     (1ULL << 6)
#define BOOT_MAPPING_ATTR_MIGRATABLE     (1ULL << 7)
#define BOOT_MAPPING_ATTR_BOOTSTRAP_ONLY (1ULL << 8)

typedef struct BOOT_MAPPING_MANIFEST_ENTRY
{
    UINT32 Category;
    UINT32 CacheType;
    UINT64 Attributes;
    HO_VIRTUAL_ADDRESS VirtStart;
    UINT64 VirtSize;
    HO_PHYSICAL_ADDRESS PhysStart;
    UINT64 PhysSize;
    UINT64 PageSize;
    UINT64 RawPteFlags;
} BOOT_MAPPING_MANIFEST_ENTRY;

typedef struct BOOT_MAPPING_MANIFEST
{
    UINT32 Magic;
    UINT16 Version;
    UINT16 EntrySize;
    UINT32 EntryCount;
    UINT32 EntryCapacity;
    UINT64 RequiredCategories;
    HO_PHYSICAL_ADDRESS EntriesPhys;
    UINT64 EntriesBytes;
} BOOT_MAPPING_MANIFEST;

static inline UINT64
BootMappingCategoryMask(UINT32 category)
{
    return (category < 64U) ? (1ULL << category) : 0ULL;
}

static inline UINT64
BootMappingManifestStorageBytes(UINT32 entryCapacity)
{
    return (UINT64)entryCapacity * sizeof(BOOT_MAPPING_MANIFEST_ENTRY);
}

static inline int
BootMappingManifestCompareEntryOrder(const BOOT_MAPPING_MANIFEST_ENTRY *lhs, const BOOT_MAPPING_MANIFEST_ENTRY *rhs)
{
    if (lhs->VirtStart < rhs->VirtStart)
        return -1;
    if (lhs->VirtStart > rhs->VirtStart)
        return 1;
    if (lhs->PhysStart < rhs->PhysStart)
        return -1;
    if (lhs->PhysStart > rhs->PhysStart)
        return 1;
    if (lhs->Category < rhs->Category)
        return -1;
    if (lhs->Category > rhs->Category)
        return 1;
    if (lhs->PageSize < rhs->PageSize)
        return -1;
    if (lhs->PageSize > rhs->PageSize)
        return 1;
    return 0;
}

static inline BOOL
BootMappingManifestEntryIsOrdered(const BOOT_MAPPING_MANIFEST_ENTRY *prev, const BOOT_MAPPING_MANIFEST_ENTRY *next)
{
    return BootMappingManifestCompareEntryOrder(prev, next) <= 0;
}

static inline BOOL
BootMappingManifestHasValidLayout(HO_PHYSICAL_ADDRESS capsuleBasePhys,
                                  UINT64 capsuleStructSize,
                                  UINT64 capsuleHeaderSize,
                                  const BOOT_MAPPING_MANIFEST *manifest)
{
    if (manifest == NULL)
        return FALSE;

    if (manifest->Magic != BOOT_MAPPING_MANIFEST_MAGIC || manifest->Version != BOOT_MAPPING_MANIFEST_VERSION)
        return FALSE;

    if (manifest->EntrySize != sizeof(BOOT_MAPPING_MANIFEST_ENTRY) || manifest->EntryCount > manifest->EntryCapacity)
        return FALSE;

    if (manifest->EntriesBytes != BootMappingManifestStorageBytes(manifest->EntryCapacity))
        return FALSE;

    if (manifest->EntryCapacity == 0)
        return manifest->EntriesPhys == 0 && manifest->EntriesBytes == 0;

    if (manifest->EntriesPhys == 0)
        return FALSE;

    HO_PHYSICAL_ADDRESS headerStart = capsuleBasePhys + capsuleStructSize;
    HO_PHYSICAL_ADDRESS headerEnd = capsuleBasePhys + capsuleHeaderSize;
    HO_PHYSICAL_ADDRESS entriesEnd = manifest->EntriesPhys + manifest->EntriesBytes;

    if (headerEnd < headerStart || entriesEnd < manifest->EntriesPhys)
        return FALSE;

    if (manifest->EntriesPhys < headerStart || entriesEnd > headerEnd)
        return FALSE;

    return TRUE;
}

/**
 * @brief Boot capsule containing information passed from the bootloader to the kernel.
 *
 * This structure encapsulates all platform and boot-time data that the kernel needs
 * to initialize system services and device drivers. The bootloader is responsible for
 * populating this capsule before transferring control to the kernel; the kernel reads
 * it during early initialization and may copy or parse its contents as needed.
 *
 * Note: Only physical addresses are exposed in this structure.
 * The kernel is responsible for constructing its own virtual mappings based on these physical addresses.
 *
 * @see bootloader documentation for the exact field layout and initialization contract.
 */
typedef struct BOOT_CAPSULE
{
    uint64_t Magic;               // 'HOS!' (0x214F5348)
    HO_PHYSICAL_ADDRESS BasePhys; // Physical base address of this structure

    // GOP
    HO_PHYSICAL_ADDRESS FramebufferPhys; // Map to BOOT_FRAMEBUFFER_VA
    enum VIDEO_MODE_TYPE VideoModeType;
    enum PIXEL_FORMAT PixelFormat;
    size_t FramebufferSize;
    uint64_t HorizontalResolution;
    uint64_t VerticalResolution;
    uint64_t PixelsPerScanLine;

    BOOT_CAPSULE_LAYOUT Layout;
    BOOT_CAPSULE_PAGE_LAYOUT PageLayout; // Actual page layout used

    HO_PHYSICAL_ADDRESS MemoryMapPhys;     // Physical address of the memory map, HHDM
    HO_PHYSICAL_ADDRESS AcpiRsdpPhys;      // Physical address of ACPI RSDP, HHDM
    HO_PHYSICAL_ADDRESS KrnlEntryPhys;     // Physical address of the kernel loaded segments, BOOT_KRNL_ENTRY_VA
    HO_PHYSICAL_ADDRESS KrnlStackPhys;     // Physical address of the kernel stack, BOOT_KRNL_STACK_VA
    HO_PHYSICAL_ADDRESS KrnlIST1StackPhys; // Physical address of the IST#1 stack, BOOT_KRNL_IST1_STACK_VA

    BOOT_MAPPING_MANIFEST MappingManifest;
    PAGE_TABLE_INFO PageTableInfo;
    CPU_CORE_LOCAL_DATA CpuInfo;
} BOOT_CAPSULE, STAGING_BLOCK, BOOT_INFO_HEADER;
