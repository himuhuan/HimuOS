/**
 * HimuOperatingSystem
 *
 * File: init_boot_mapping.c
 * Description: Boot mapping manifest import and validation.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "init_internal.h"
#include <kernel/init.h>
#include <kernel/hodbg.h>
#include <arch/amd64/acpi.h>
#include <arch/amd64/efi_mem.h>
#include <libc/string.h>

typedef struct BOOT_MAPPING_IMPORT_STATE
{
    const BOOT_MAPPING_MANIFEST_ENTRY *Entries;
    UINT32 EntryCount;
    UINT64 SeenCategories;
    BOOL StrictValidated;
} BOOT_MAPPING_IMPORT_STATE;

static BOOT_MAPPING_IMPORT_STATE gBootMappingImportState;

static const char *BootMappingCategoryName(UINT32 category);
static BOOL IsBootMappingUmbrellaCategory(UINT32 category);
static const BOOT_MAPPING_MANIFEST_ENTRY *GetBootMappingManifestEntries(const STAGING_BLOCK *block);
static BOOL ManifestCategoryCoversRange(const STAGING_BLOCK *block,
                                        UINT32 category,
                                        UINT64 virtStart,
                                        UINT64 size,
                                        UINT64 physStart);
static BOOL ManifestCoversLeafMapping(const STAGING_BLOCK *block, UINT64 virtStart, UINT64 size, UINT64 physStart);
static BOOL ValidateHighHalfLeafMappings(const STAGING_BLOCK *block);
static BOOL WalkHighHalfLeafMappings(const STAGING_BLOCK *block,
                                     UINT64 tablePhys,
                                     UINT64 level,
                                     UINT64 virtBase,
                                     UINT32 *unexpectedCount,
                                     UINT32 *loggedCount);
static UINT64 CanonicalizeVirtAddress(UINT64 virt);
static UINT64 ReadCr3Phys(void);

HO_STATUS
ImportBootMappings(STAGING_BLOCK *block)
{
    if (block == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!BootMappingManifestHasValidLayout(block->BasePhys, sizeof(BOOT_CAPSULE), block->Layout.HeaderSize,
                                           &block->MappingManifest))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] boot mapping manifest header invalid\n");
        return EC_INVALID_STATE;
    }

    const BOOT_MAPPING_MANIFEST_ENTRY *entries = GetBootMappingManifestEntries(block);
    if (entries == NULL)
        return EC_INVALID_STATE;

    memset(&gBootMappingImportState, 0, sizeof(gBootMappingImportState));
    gBootMappingImportState.Entries = entries;
    gBootMappingImportState.EntryCount = block->MappingManifest.EntryCount;

    for (UINT32 idx = 0; idx < block->MappingManifest.EntryCount; ++idx)
    {
        const BOOT_MAPPING_MANIFEST_ENTRY *entry = &entries[idx];
        UINT64 entryVirtEnd = entry->VirtStart + entry->VirtSize;
        UINT64 entryPhysEnd = entry->PhysStart + entry->PhysSize;

        if (entryVirtEnd < entry->VirtStart || entryPhysEnd < entry->PhysStart)
        {
            klog(KLOG_LEVEL_ERROR, "[VM] manifest entry %u wraps around\n", idx);
            return EC_INVALID_STATE;
        }

        if ((entry->Attributes & BOOT_MAPPING_ATTR_BOOT_IMPORTED) == 0)
        {
            klog(KLOG_LEVEL_ERROR, "[VM] manifest entry %u (%s) missing BOOT_IMPORTED\n", idx,
                 BootMappingCategoryName(entry->Category));
            return EC_INVALID_STATE;
        }

        if (idx > 0 && !BootMappingManifestEntryIsOrdered(&entries[idx - 1], entry))
        {
            klog(KLOG_LEVEL_ERROR, "[VM] manifest entry order invalid at %u\n", idx);
            return EC_INVALID_STATE;
        }

        for (UINT32 prevIdx = 0; prevIdx < idx; ++prevIdx)
        {
            const BOOT_MAPPING_MANIFEST_ENTRY *prev = &entries[prevIdx];
            UINT64 prevVirtEnd = prev->VirtStart + prev->VirtSize;
            BOOL overlapsVirt = !(entryVirtEnd <= prev->VirtStart || prevVirtEnd <= entry->VirtStart);

            if (!overlapsVirt)
                continue;

            if (prev->Category == entry->Category)
            {
                klog(KLOG_LEVEL_ERROR, "[VM] duplicate manifest ownership in category=%s at VA=%p\n",
                     BootMappingCategoryName(entry->Category), (void *)(UINTN)entry->VirtStart);
                return EC_INVALID_STATE;
            }

            if (!IsBootMappingUmbrellaCategory(prev->Category) && !IsBootMappingUmbrellaCategory(entry->Category))
            {
                klog(KLOG_LEVEL_ERROR, "[VM] conflicting manifest ownership %s vs %s at VA=%p\n",
                     BootMappingCategoryName(prev->Category), BootMappingCategoryName(entry->Category),
                     (void *)(UINTN)entry->VirtStart);
                return EC_INVALID_STATE;
            }
        }

        gBootMappingImportState.SeenCategories |= BootMappingCategoryMask(entry->Category);
    }

    if ((block->MappingManifest.RequiredCategories & gBootMappingImportState.SeenCategories) !=
        block->MappingManifest.RequiredCategories)
    {
           klog(KLOG_LEVEL_ERROR, "[VM] manifest missing required categories: required=0x%x seen=0x%x\n",
             block->MappingManifest.RequiredCategories, gBootMappingImportState.SeenCategories);
        return EC_INVALID_STATE;
    }

    UINT64 codeSizeAligned = HO_ALIGN_UP(block->Layout.KrnlCodeSize, PAGE_4KB);
    UINT64 dataSizeAligned = HO_ALIGN_UP(block->Layout.KrnlDataSize, PAGE_4KB);
    UINT64 capsuleBytes = block->PageLayout.TotalPages << PAGE_SHIFT;

    if (!ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_BOOT_CAPSULE, HHDM_BASE_VA + block->BasePhys,
                                     capsuleBytes, block->BasePhys))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] manifest missing boot capsule mapping\n");
        return EC_INVALID_STATE;
    }

    if (!ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_PAGE_TABLES, HHDM_BASE_VA + block->PageTableInfo.Ptr,
                                     block->PageTableInfo.Size, block->PageTableInfo.Ptr))
    {
        for (UINT32 idx = 0; idx < block->MappingManifest.EntryCount; ++idx)
        {
            const BOOT_MAPPING_MANIFEST_ENTRY *entry = &entries[idx];
            if (entry->Category != BOOT_MAPPING_CATEGORY_PAGE_TABLES)
                continue;

            klog(KLOG_LEVEL_ERROR, "[VM] page-table manifest entry: VA=[%p,%p) PA=[%p,%p) size=%u page=%u\n",
                 (void *)(UINTN)entry->VirtStart, (void *)(UINTN)(entry->VirtStart + entry->VirtSize),
                 (void *)(UINTN)entry->PhysStart, (void *)(UINTN)(entry->PhysStart + entry->PhysSize),
                 entry->VirtSize, entry->PageSize);
        }
        klog(KLOG_LEVEL_ERROR, "[VM] manifest missing page-table range\n");
        return EC_INVALID_STATE;
    }

    if (!ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_KERNEL_TEXT, KRNL_BASE_VA, codeSizeAligned,
                                     block->KrnlEntryPhys))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] manifest missing kernel text mapping\n");
        return EC_INVALID_STATE;
    }

    if (block->Layout.KrnlDataSize > 0 &&
        !ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_KERNEL_DATA, KRNL_BASE_VA + codeSizeAligned,
                                     dataSizeAligned, block->KrnlEntryPhys + codeSizeAligned))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] manifest missing kernel data mapping\n");
        return EC_INVALID_STATE;
    }

    if (!ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_BOOT_STACK, KRNL_STACK_VA, block->Layout.KrnlStackSize,
                                     block->KrnlStackPhys))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] manifest missing boot stack mapping\n");
        return EC_INVALID_STATE;
    }

    if (!ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_IST_STACK, KRNL_IST1_STACK_VA,
                                     block->Layout.IST1StackSize, block->KrnlIST1StackPhys))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] manifest missing IST stack mapping\n");
        return EC_INVALID_STATE;
    }

    if (block->FramebufferSize > 0 &&
        !ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_FRAMEBUFFER, MMIO_BASE_VA, block->FramebufferSize,
                                     block->FramebufferPhys))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] manifest missing framebuffer mapping\n");
        return EC_INVALID_STATE;
    }

    if (block->AcpiRsdpPhys != 0 &&
        !ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_ACPI, HHDM_BASE_VA + block->AcpiRsdpPhys,
                                     sizeof(ACPI_RSDP), block->AcpiRsdpPhys))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] manifest missing ACPI RSDP mapping\n");
        return EC_INVALID_STATE;
    }

    EFI_MEMORY_MAP *map = (EFI_MEMORY_MAP *)HHDM_PHYS2VIRT(block->MemoryMapPhys);
    if (!map || map->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) || map->DescriptorSize == 0)
        return EC_INVALID_STATE;

    UINT64 descriptorCount = map->DescriptorTotalSize / map->DescriptorSize;
    for (UINT64 idx = 0; idx < descriptorCount; ++idx)
    {
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map->Segs + idx * map->DescriptorSize);
        if (desc == NULL || desc->NumberOfPages == 0)
            continue;

        UINT64 mapSize = desc->NumberOfPages << PAGE_SHIFT;
        if (!ManifestCategoryCoversRange(block, BOOT_MAPPING_CATEGORY_HHDM, HHDM_BASE_VA + desc->PhysicalStart, mapSize,
                                         desc->PhysicalStart))
        {
              klog(KLOG_LEVEL_ERROR, "[VM] manifest missing HHDM extent for phys=%p pages=%u\n",
                 (void *)(UINTN)desc->PhysicalStart, desc->NumberOfPages);
            return EC_INVALID_STATE;
        }
    }

    if ((ReadCr3Phys() & PAGE_MASK) != (block->PageTableInfo.Ptr & PAGE_MASK))
    {
        klog(KLOG_LEVEL_ERROR, "[VM] active CR3=%p does not match boot page-table root=%p\n",
             (void *)(UINTN)(ReadCr3Phys() & PAGE_MASK), (void *)(UINTN)(block->PageTableInfo.Ptr & PAGE_MASK));
        return EC_INVALID_STATE;
    }

    if (!ValidateHighHalfLeafMappings(block))
        return EC_INVALID_STATE;

    gBootMappingImportState.StrictValidated = TRUE;
        klog(KLOG_LEVEL_INFO, "[VM] imported boot mapping manifest: entries=%u required=0x%x seen=0x%x\n",
         gBootMappingImportState.EntryCount, block->MappingManifest.RequiredCategories,
         gBootMappingImportState.SeenCategories);
    return EC_SUCCESS;
}

static const char *
BootMappingCategoryName(UINT32 category)
{
    switch (category)
    {
        case BOOT_MAPPING_CATEGORY_BOOTSTRAP_IDENTITY:
            return "bootstrap-identity";
        case BOOT_MAPPING_CATEGORY_HHDM:
            return "hhdm";
        case BOOT_MAPPING_CATEGORY_BOOT_CAPSULE:
            return "boot-capsule";
        case BOOT_MAPPING_CATEGORY_ACPI:
            return "acpi";
        case BOOT_MAPPING_CATEGORY_KERNEL_TEXT:
            return "kernel-text";
        case BOOT_MAPPING_CATEGORY_KERNEL_DATA:
            return "kernel-data";
        case BOOT_MAPPING_CATEGORY_BOOT_STACK:
            return "boot-stack";
        case BOOT_MAPPING_CATEGORY_IST_STACK:
            return "ist-stack";
        case BOOT_MAPPING_CATEGORY_FRAMEBUFFER:
            return "framebuffer";
        case BOOT_MAPPING_CATEGORY_PAGE_TABLES:
            return "page-tables";
        case BOOT_MAPPING_CATEGORY_LAPIC_MMIO:
            return "lapic-mmio";
        case BOOT_MAPPING_CATEGORY_HPET_MMIO:
            return "hpet-mmio";
        default:
            return "unknown";
    }
}

static BOOL
IsBootMappingUmbrellaCategory(UINT32 category)
{
    return category == BOOT_MAPPING_CATEGORY_BOOTSTRAP_IDENTITY || category == BOOT_MAPPING_CATEGORY_HHDM;
}

static const BOOT_MAPPING_MANIFEST_ENTRY *
GetBootMappingManifestEntries(const STAGING_BLOCK *block)
{
    if (block == NULL || block->MappingManifest.EntriesPhys == 0)
        return NULL;
    return (const BOOT_MAPPING_MANIFEST_ENTRY *)HHDM_PHYS2VIRT(block->MappingManifest.EntriesPhys);
}

static BOOL
ManifestCategoryCoversRange(const STAGING_BLOCK *block, UINT32 category, UINT64 virtStart, UINT64 size, UINT64 physStart)
{
    const BOOT_MAPPING_MANIFEST_ENTRY *entries = GetBootMappingManifestEntries(block);
    if (entries == NULL)
        return FALSE;

    UINT64 targetVirt = virtStart;
    UINT64 targetPhys = physStart;
    UINT64 targetVirtEnd = virtStart + size;
    if (targetVirtEnd < virtStart)
        return FALSE;

    while (targetVirt < targetVirtEnd)
    {
        BOOL progressed = FALSE;

        for (UINT32 idx = 0; idx < block->MappingManifest.EntryCount; ++idx)
        {
            const BOOT_MAPPING_MANIFEST_ENTRY *entry = &entries[idx];
            UINT64 entryVirtEnd;
            UINT64 entryPhysEnd;
            UINT64 offsetWithinEntry;
            UINT64 availableBytes;

            if (entry->Category != category)
                continue;

            entryVirtEnd = entry->VirtStart + entry->VirtSize;
            entryPhysEnd = entry->PhysStart + entry->PhysSize;
            if (entryVirtEnd < entry->VirtStart || entryPhysEnd < entry->PhysStart)
                return FALSE;

            if (entry->VirtStart > targetVirt || entryVirtEnd <= targetVirt)
                continue;

            offsetWithinEntry = targetVirt - entry->VirtStart;
            if (entry->PhysStart + offsetWithinEntry != targetPhys)
                continue;

            availableBytes = entryVirtEnd - targetVirt;
            if (availableBytes == 0)
                continue;

            if (targetVirt + availableBytes > targetVirtEnd)
                availableBytes = targetVirtEnd - targetVirt;

            targetVirt += availableBytes;
            targetPhys += availableBytes;
            progressed = TRUE;
            break;
        }

        if (!progressed)
            return FALSE;
    }

    return TRUE;
}

static BOOL
ManifestCoversLeafMapping(const STAGING_BLOCK *block, UINT64 virtStart, UINT64 size, UINT64 physStart)
{
    const BOOT_MAPPING_MANIFEST_ENTRY *entries = GetBootMappingManifestEntries(block);
    if (entries == NULL)
        return FALSE;

    UINT64 virtEnd = virtStart + size;
    UINT64 physEnd = physStart + size;
    for (UINT32 idx = 0; idx < block->MappingManifest.EntryCount; ++idx)
    {
        const BOOT_MAPPING_MANIFEST_ENTRY *entry = &entries[idx];
        UINT64 entryVirtEnd = entry->VirtStart + entry->VirtSize;
        UINT64 entryPhysEnd = entry->PhysStart + entry->PhysSize;
        if (entry->VirtStart <= virtStart && entryVirtEnd >= virtEnd && entry->PhysStart <= physStart &&
            entryPhysEnd >= physEnd)
            return TRUE;
    }

    return FALSE;
}

static BOOL
ValidateHighHalfLeafMappings(const STAGING_BLOCK *block)
{
    UINT32 unexpectedCount = 0;
    UINT32 loggedCount = 0;

    if (!WalkHighHalfLeafMappings(block, block->PageTableInfo.Ptr, 4, 0, &unexpectedCount, &loggedCount))
        return FALSE;

    if (unexpectedCount != 0)
    {
        klog(KLOG_LEVEL_ERROR, "[VM] found %u unexplained high-half leaf mappings\n", unexpectedCount);
        return FALSE;
    }

    return TRUE;
}

static BOOL
WalkHighHalfLeafMappings(const STAGING_BLOCK *block,
                         UINT64 tablePhys,
                         UINT64 level,
                         UINT64 virtBase,
                         UINT32 *unexpectedCount,
                         UINT32 *loggedCount)
{
    PAGE_TABLE_ENTRY *table = (PAGE_TABLE_ENTRY *)HHDM_PHYS2VIRT(tablePhys);
    if (table == NULL)
        return FALSE;

    for (UINT64 idx = 0; idx < ENTRIES_PER_TABLE; ++idx)
    {
        UINT64 entry = table[idx];
        if ((entry & PTE_PRESENT) == 0)
            continue;

        UINT64 nextVirtBase = virtBase;
        UINT64 leafSize = 0;

        if (level == 4)
        {
            nextVirtBase = CanonicalizeVirtAddress(idx << PML4_SHIFT);
            if ((nextVirtBase & (1ULL << 63)) == 0)
                continue;
        }
        else if (level == 3)
        {
            nextVirtBase = virtBase | (idx << PDPT_SHIFT);
            if (entry & PTE_PAGE_SIZE)
                leafSize = PAGE_1GB;
        }
        else if (level == 2)
        {
            nextVirtBase = virtBase | (idx << PD_SHIFT);
            if (entry & PTE_PAGE_SIZE)
                leafSize = PAGE_2MB;
        }
        else
        {
            nextVirtBase = virtBase | (idx << PT_SHIFT);
            leafSize = PAGE_4KB;
        }

        if (leafSize != 0)
        {
            UINT64 leafVirt = CanonicalizeVirtAddress(nextVirtBase);
            UINT64 leafPhys = entry & ADDR_MASK_48BIT & PAGE_MASK;
            if (!ManifestCoversLeafMapping(block, leafVirt, leafSize, leafPhys))
            {
                (*unexpectedCount)++;
                if (*loggedCount < 8)
                {
                    klog(KLOG_LEVEL_ERROR, "[VM] unexplained high-half leaf mapping: VA=[%p,%p) PA=%p size=%u\n",
                         (void *)(UINTN)leafVirt, (void *)(UINTN)(leafVirt + leafSize), (void *)(UINTN)leafPhys,
                         leafSize);
                    (*loggedCount)++;
                }
            }
            continue;
        }

        if (!WalkHighHalfLeafMappings(block, entry & ADDR_MASK_48BIT & PAGE_MASK, level - 1, nextVirtBase,
                                      unexpectedCount, loggedCount))
            return FALSE;
    }

    return TRUE;
}

static UINT64
CanonicalizeVirtAddress(UINT64 virt)
{
    if (virt & (1ULL << 47))
        virt |= 0xFFFF000000000000ULL;
    return virt;
}

static UINT64
ReadCr3Phys(void)
{
    UINT64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}
