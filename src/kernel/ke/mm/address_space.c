/**
 * HimuOperatingSystem
 *
 * File: ke/mm/address_space.c
 * Description:
 * Imported kernel address space and phase-one page-table HAL.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/mm.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

#define KE_PT_ALLOWED_LEAF_FLAGS                                                                                       \
    (PTE_WRITABLE | PTE_USER | PTE_WRITETHROUGH | PTE_CACHE_DISABLE | PTE_GLOBAL | PTE_NO_EXECUTE)
#define KE_PT_PHYS_ADDR_MASK 0x000FFFFFFFFFF000ULL

typedef struct KE_PT_WALK
{
    PAGE_TABLE_ENTRY *Pml4;
    PAGE_TABLE_ENTRY *Pdpt;
    PAGE_TABLE_ENTRY *Pd;
    PAGE_TABLE_ENTRY *Pt;
    PAGE_TABLE_ENTRY *Pml4Entry;
    PAGE_TABLE_ENTRY *PdptEntry;
    PAGE_TABLE_ENTRY *PdEntry;
    PAGE_TABLE_ENTRY *PtEntry;
    PAGE_TABLE_ENTRY *LeafEntry;
    PAGE_TABLE_ENTRY LeafValue;
    HO_PHYSICAL_ADDRESS LeafPhysBase;
    HO_VIRTUAL_ADDRESS LeafVirtBase;
    uint64_t PageSize;
    uint8_t Level;
} KE_PT_WALK;

typedef struct KE_NEW_TABLE
{
    PAGE_TABLE_ENTRY *ParentEntry;
    HO_PHYSICAL_ADDRESS TablePhys;
} KE_NEW_TABLE;

static KE_KERNEL_ADDRESS_SPACE gKernelAddressSpace;

static inline HO_PHYSICAL_ADDRESS
KiReadCr3(void)
{
    uint64_t cr3;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(cr3));
    return cr3 & PAGE_MASK;
}

static inline void
KiInvalidatePage(HO_VIRTUAL_ADDRESS virtAddr)
{
    __asm__ __volatile__("invlpg (%0)" : : "r"((void *)(uint64_t)virtAddr) : "memory");
}

static inline PAGE_TABLE_ENTRY *
KiTableFromPhys(HO_PHYSICAL_ADDRESS physAddr)
{
    return (PAGE_TABLE_ENTRY *)(uint64_t)HHDM_PHYS2VIRT(physAddr);
}

static inline BOOL
KiIsBootOwnedLifetime(uint8_t lifetime)
{
    return lifetime == BOOT_MAPPING_LIFETIME_TEMPORARY || lifetime == BOOT_MAPPING_LIFETIME_RETAINED;
}

static inline HO_PHYSICAL_ADDRESS
KiLeafPhysBase(PAGE_TABLE_ENTRY entry, uint64_t pageSize)
{
    return (entry & KE_PT_PHYS_ADDR_MASK) & ~(pageSize - 1);
}

static int
KiCompareImportedRegions(const KE_IMPORTED_REGION *left, const KE_IMPORTED_REGION *right)
{
    if (left->VirtualStart < right->VirtualStart)
        return -1;
    if (left->VirtualStart > right->VirtualStart)
        return 1;
    if (left->Size < right->Size)
        return -1;
    if (left->Size > right->Size)
        return 1;
    if (left->Type < right->Type)
        return -1;
    if (left->Type > right->Type)
        return 1;
    return 0;
}

static void
KiSortImportedRegions(KE_IMPORTED_REGION *regions, uint32_t count)
{
    for (uint32_t i = 1; i < count; ++i)
    {
        KE_IMPORTED_REGION key = regions[i];
        uint32_t j = i;
        while (j > 0 && KiCompareImportedRegions(&key, &regions[j - 1]) < 0)
        {
            regions[j] = regions[j - 1];
            --j;
        }
        regions[j] = key;
    }
}

static void
KiRollbackNewTables(KE_NEW_TABLE *tables, uint32_t tableCount)
{
    while (tableCount > 0)
    {
        --tableCount;
        *tables[tableCount].ParentEntry = 0;
        (void)KePmmFreePages(tables[tableCount].TablePhys, 1);
    }
}

static HO_STATUS
KiEnsureChildTable(PAGE_TABLE_ENTRY *parentEntry,
                   KE_NEW_TABLE *newTables,
                   uint32_t *newTableCount,
                   PAGE_TABLE_ENTRY **outTable)
{
    if (!parentEntry || !newTableCount || !outTable)
        return EC_ILLEGAL_ARGUMENT;

    if ((*parentEntry & PTE_PRESENT) == 0)
    {
        HO_PHYSICAL_ADDRESS tablePhys;
        HO_STATUS status = KePmmAllocPages(1, NULL, &tablePhys);
        if (status != EC_SUCCESS)
            return status;

        PAGE_TABLE_ENTRY *table = KiTableFromPhys(tablePhys);
        memset(table, 0, PAGE_4KB);

        *parentEntry = tablePhys | PTE_PRESENT | PTE_WRITABLE;
        newTables[*newTableCount].ParentEntry = parentEntry;
        newTables[*newTableCount].TablePhys = tablePhys;
        ++(*newTableCount);
        *outTable = table;
        return EC_SUCCESS;
    }

    if ((*parentEntry & PTE_PAGE_SIZE) != 0)
        return EC_NOT_SUPPORTED;

    *outTable = KiTableFromPhys(*parentEntry & PAGE_MASK);
    return EC_SUCCESS;
}

static HO_STATUS
KiWalkImportedRoot(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr, KE_PT_WALK *walk)
{
    if (!space || !walk)
        return EC_ILLEGAL_ARGUMENT;
    if (!space->Initialized)
        return EC_INVALID_STATE;
    if (!HO_IS_ALIGNED(virtAddr, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;

    memset(walk, 0, sizeof(*walk));

    walk->Pml4 = KiTableFromPhys(space->RootPageTablePhys);
    walk->Pml4Entry = &walk->Pml4[PML4_INDEX(virtAddr)];
    if ((*walk->Pml4Entry & PTE_PRESENT) == 0)
        return EC_SUCCESS;

    walk->Pdpt = KiTableFromPhys(*walk->Pml4Entry & PAGE_MASK);
    walk->PdptEntry = &walk->Pdpt[PDPT_INDEX(virtAddr)];
    if ((*walk->PdptEntry & PTE_PRESENT) == 0)
        return EC_SUCCESS;

    if ((*walk->PdptEntry & PTE_PAGE_SIZE) != 0)
    {
        walk->LeafEntry = walk->PdptEntry;
        walk->LeafValue = *walk->PdptEntry;
        walk->Level = 3;
        walk->PageSize = PAGE_1GB;
        walk->LeafVirtBase = virtAddr & ~(PAGE_1GB - 1);
        walk->LeafPhysBase = KiLeafPhysBase(walk->LeafValue, walk->PageSize) + (virtAddr & (PAGE_1GB - 1));
        return EC_SUCCESS;
    }

    walk->Pd = KiTableFromPhys(*walk->PdptEntry & PAGE_MASK);
    walk->PdEntry = &walk->Pd[PD_INDEX(virtAddr)];
    if ((*walk->PdEntry & PTE_PRESENT) == 0)
        return EC_SUCCESS;

    if ((*walk->PdEntry & PTE_PAGE_SIZE) != 0)
    {
        walk->LeafEntry = walk->PdEntry;
        walk->LeafValue = *walk->PdEntry;
        walk->Level = 2;
        walk->PageSize = PAGE_2MB;
        walk->LeafVirtBase = virtAddr & ~(PAGE_2MB - 1);
        walk->LeafPhysBase = KiLeafPhysBase(walk->LeafValue, walk->PageSize) + (virtAddr & (PAGE_2MB - 1));
        return EC_SUCCESS;
    }

    walk->Pt = KiTableFromPhys(*walk->PdEntry & PAGE_MASK);
    walk->PtEntry = &walk->Pt[PT_INDEX(virtAddr)];
    if ((*walk->PtEntry & PTE_PRESENT) == 0)
        return EC_SUCCESS;

    walk->LeafEntry = walk->PtEntry;
    walk->LeafValue = *walk->PtEntry;
    walk->Level = 1;
    walk->PageSize = PAGE_4KB;
    walk->LeafVirtBase = virtAddr;
    walk->LeafPhysBase = walk->LeafValue & KE_PT_PHYS_ADDR_MASK;
    return EC_SUCCESS;
}

static HO_STATUS
KiFindScratchHole(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS *outVirtAddr)
{
    if (!space || !outVirtAddr)
        return EC_ILLEGAL_ARGUMENT;

    HO_VIRTUAL_ADDRESS candidate = HHDM_BASE_VA;
    const HO_VIRTUAL_ADDRESS limit = MMIO_BASE_VA;

    while (candidate < limit)
    {
        BOOL advancedByRegion = FALSE;

        for (uint32_t idx = 0; idx < space->RegionCount; ++idx)
        {
            const KE_IMPORTED_REGION *region = &space->Regions[idx];
            if (candidate < region->VirtualStart || candidate >= region->VirtualEndExclusive)
                continue;

            candidate = HO_ALIGN_UP(region->VirtualEndExclusive, PAGE_4KB);
            advancedByRegion = TRUE;
            break;
        }

        if (advancedByRegion)
        {
            continue;
        }

        KE_PT_WALK walk;
        HO_STATUS status = KiWalkImportedRoot(space, candidate, &walk);
        if (status != EC_SUCCESS)
            return status;

        if (!walk.LeafEntry)
        {
            *outVirtAddr = candidate;
            return EC_SUCCESS;
        }

        candidate = HO_ALIGN_UP(walk.LeafVirtBase + walk.PageSize, PAGE_4KB);
    }

    return EC_NOT_SUPPORTED;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeImportKernelAddressSpace(struct BOOT_CAPSULE *capsule, const BOOT_MAPPING_MANIFEST_HEADER *manifest)
{
    if (!capsule || !manifest)
        return EC_ILLEGAL_ARGUMENT;
    if (gKernelAddressSpace.Initialized)
        return EC_INVALID_STATE;

    HO_PHYSICAL_ADDRESS activeRoot = KiReadCr3();
    HO_PHYSICAL_ADDRESS handoffRoot = capsule->PageTableInfo.Ptr & PAGE_MASK;

    if (activeRoot != handoffRoot)
    {
        klog(KLOG_LEVEL_ERROR, "[MM] imported root mismatch: active=%p handoff=%p\n", (void *)(uint64_t)activeRoot,
             (void *)(uint64_t)handoffRoot);
        return EC_INVALID_STATE;
    }

    KE_IMPORTED_REGION *regions = NULL;
    HO_PHYSICAL_ADDRESS regionArrayPhys = 0;
    uint64_t regionArrayBytes = (uint64_t)manifest->EntryCount * sizeof(KE_IMPORTED_REGION);

    if (manifest->EntryCount != 0)
    {
        uint64_t regionPages = HO_ALIGN_UP(regionArrayBytes, PAGE_4KB) >> PAGE_SHIFT;
        HO_STATUS allocStatus = KePmmAllocPages(regionPages, NULL, &regionArrayPhys);
        if (allocStatus != EC_SUCCESS)
            return allocStatus;

        regions = (KE_IMPORTED_REGION *)(uint64_t)HHDM_PHYS2VIRT(regionArrayPhys);
        memset(regions, 0, regionPages << PAGE_SHIFT);
    }

    const BOOT_MAPPING_MANIFEST_ENTRY *entries = BootMappingManifestConstEntries(manifest);
    uint32_t bootOwnedCount = 0;
    uint32_t pinnedCount = 0;

    for (uint32_t idx = 0; idx < manifest->EntryCount; ++idx)
    {
        KE_IMPORTED_REGION *region = &regions[idx];
        const BOOT_MAPPING_MANIFEST_ENTRY *entry = &entries[idx];

        region->VirtualStart = entry->VirtualStart;
        region->VirtualEndExclusive = entry->VirtualStart + entry->Size;
        region->PhysicalStart = entry->PhysicalStart;
        region->PhysicalEndExclusive = entry->PhysicalStart + entry->Size;
        region->Size = entry->Size;
        region->Attributes = entry->Attributes;
        region->ProvenanceValue = entry->ProvenanceValue;
        region->Type = entry->Type;
        region->Provenance = entry->Provenance;
        region->Lifetime = entry->Lifetime;
        region->Granularity = entry->Granularity;
        region->BootOwned = KiIsBootOwnedLifetime(entry->Lifetime);
        region->Pinned = TRUE;

        if (region->BootOwned)
            ++bootOwnedCount;
        if (region->Pinned)
            ++pinnedCount;
    }

    KiSortImportedRegions(regions, manifest->EntryCount);

    for (uint32_t idx = 0; idx < manifest->EntryCount; ++idx)
    {
        const KE_IMPORTED_REGION *region = &regions[idx];
        if (region->BootOwned && !region->Pinned)
            return EC_INVALID_STATE;
    }

    memset(&gKernelAddressSpace, 0, sizeof(gKernelAddressSpace));
    gKernelAddressSpace.RootPageTablePhys = handoffRoot;
    gKernelAddressSpace.ImportedRootInfo = capsule->PageTableInfo;
    gKernelAddressSpace.Regions = regions;
    gKernelAddressSpace.RegionArrayPhys = regionArrayPhys;
    gKernelAddressSpace.RegionArrayBytes = regionArrayBytes;
    gKernelAddressSpace.RegionCount = manifest->EntryCount;
    gKernelAddressSpace.BootOwnedRegionCount = bootOwnedCount;
    gKernelAddressSpace.PinnedRegionCount = pinnedCount;
    gKernelAddressSpace.Initialized = TRUE;

    klog(KLOG_LEVEL_INFO, "[MM] imported kernel root OK: root=%p regions=%u boot_owned=%u pinned=%u\n",
         (void *)(uint64_t)gKernelAddressSpace.RootPageTablePhys, gKernelAddressSpace.RegionCount,
         gKernelAddressSpace.BootOwnedRegionCount, gKernelAddressSpace.PinnedRegionCount);

    uint32_t elidedRegionCount = 0;
    for (uint32_t idx = 0; idx < gKernelAddressSpace.RegionCount; ++idx)
    {
        const KE_IMPORTED_REGION *region = &gKernelAddressSpace.Regions[idx];
        if (idx >= 16 && region->Type == BOOT_MAPPING_REGION_HHDM)
        {
            ++elidedRegionCount;
            continue;
        }

        // Too verbose
        /* klog(KLOG_LEVEL_INFO,
             "[MM] import region[%u]: va=[%p,%p) pa=[%p,%p) type=%u life=%u gran=%u pinned=%u boot_owned=%u\n", idx,
             (void *)(uint64_t)region->VirtualStart, (void *)(uint64_t)region->VirtualEndExclusive,
             (void *)(uint64_t)region->PhysicalStart, (void *)(uint64_t)region->PhysicalEndExclusive, region->Type,
             region->Lifetime, region->Granularity, region->Pinned, region->BootOwned); */
    }

    if (elidedRegionCount != 0)
    {
        klog(KLOG_LEVEL_INFO, "[MM] import region log elided %u additional HHDM entries\n", elidedRegionCount);
    }

    return EC_SUCCESS;
}

HO_KERNEL_API const KE_KERNEL_ADDRESS_SPACE *
KeGetKernelAddressSpace(void)
{
    return gKernelAddressSpace.Initialized ? &gKernelAddressSpace : NULL;
}

HO_KERNEL_API const KE_IMPORTED_REGION *
KeFindImportedRegion(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr)
{
    if (!space || !space->Initialized)
        return NULL;

    const KE_IMPORTED_REGION *best = NULL;
    for (uint32_t idx = 0; idx < space->RegionCount; ++idx)
    {
        const KE_IMPORTED_REGION *region = &space->Regions[idx];
        if (virtAddr < region->VirtualStart || virtAddr >= region->VirtualEndExclusive)
            continue;

        if (!best || region->Size < best->Size)
            best = region;
    }

    return best;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KePtQueryPage(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr, KE_PT_MAPPING *outMapping)
{
    if (!space || !outMapping)
        return EC_ILLEGAL_ARGUMENT;

    KE_PT_WALK walk;
    HO_STATUS status = KiWalkImportedRoot(space, virtAddr, &walk);
    if (status != EC_SUCCESS)
        return status;

    memset(outMapping, 0, sizeof(*outMapping));
    if (!walk.LeafEntry)
        return EC_SUCCESS;

    outMapping->Present = TRUE;
    outMapping->LargeLeaf = walk.Level > 1;
    outMapping->Level = walk.Level;
    outMapping->PageSize = walk.PageSize;
    outMapping->PhysicalBase = walk.LeafPhysBase;
    outMapping->Attributes = walk.LeafValue;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeDiagnoseVirtualAddress(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr, KE_VA_DIAGNOSIS *outDiagnosis)
{
    if (!outDiagnosis)
        return EC_ILLEGAL_ARGUMENT;

    memset(outDiagnosis, 0, sizeof(*outDiagnosis));
    outDiagnosis->VirtualAddress = virtAddr;
    outDiagnosis->ImportedStatus = EC_INVALID_STATE;
    outDiagnosis->PtStatus = EC_INVALID_STATE;
    outDiagnosis->KvaStatus = EC_INVALID_STATE;
    outDiagnosis->AllocatorStatus = EC_INVALID_STATE;

    const KE_KERNEL_ADDRESS_SPACE *effectiveSpace = space;
    if (!effectiveSpace)
        effectiveSpace = KeGetKernelAddressSpace();

    if (effectiveSpace && effectiveSpace->Initialized)
    {
        outDiagnosis->ImportedRegion = KeFindImportedRegion(effectiveSpace, virtAddr);
        outDiagnosis->ImportedRegionMatched = outDiagnosis->ImportedRegion != NULL;
        outDiagnosis->ImportedStatus = EC_SUCCESS;
        outDiagnosis->PtStatus = KePtQueryPage(effectiveSpace, HO_ALIGN_DOWN(virtAddr, PAGE_4KB), &outDiagnosis->PtMapping);
    }

    outDiagnosis->KvaStatus = KeKvaClassifyAddress(virtAddr, &outDiagnosis->KvaInfo);
    if (outDiagnosis->KvaStatus == EC_SUCCESS && outDiagnosis->KvaInfo.Kind == KE_KVA_ADDRESS_ACTIVE_HEAP)
    {
        outDiagnosis->AllocatorStatus = KeAllocatorDiagnoseAddress(virtAddr, &outDiagnosis->AllocatorInfo);
    }
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KePtMapPage(const KE_KERNEL_ADDRESS_SPACE *space,
            HO_VIRTUAL_ADDRESS virtAddr,
            HO_PHYSICAL_ADDRESS physAddr,
            uint64_t attributes)
{
    if (!space)
        return EC_ILLEGAL_ARGUMENT;
    if (!space->Initialized)
        return EC_INVALID_STATE;
    if (!HO_IS_ALIGNED(virtAddr, PAGE_4KB) || !HO_IS_ALIGNED(physAddr, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;

    KE_NEW_TABLE newTables[3];
    uint32_t newTableCount = 0;
    memset(newTables, 0, sizeof(newTables));

    PAGE_TABLE_ENTRY *pml4 = KiTableFromPhys(space->RootPageTablePhys);
    PAGE_TABLE_ENTRY *pml4Entry = &pml4[PML4_INDEX(virtAddr)];
    PAGE_TABLE_ENTRY *pdpt = NULL;

    HO_STATUS status = KiEnsureChildTable(pml4Entry, newTables, &newTableCount, &pdpt);
    if (status != EC_SUCCESS)
        return status;

    PAGE_TABLE_ENTRY *pdptEntry = &pdpt[PDPT_INDEX(virtAddr)];
    PAGE_TABLE_ENTRY *pd = NULL;
    status = KiEnsureChildTable(pdptEntry, newTables, &newTableCount, &pd);
    if (status != EC_SUCCESS)
    {
        KiRollbackNewTables(newTables, newTableCount);
        return status;
    }

    PAGE_TABLE_ENTRY *pdEntry = &pd[PD_INDEX(virtAddr)];
    PAGE_TABLE_ENTRY *pt = NULL;
    status = KiEnsureChildTable(pdEntry, newTables, &newTableCount, &pt);
    if (status != EC_SUCCESS)
    {
        KiRollbackNewTables(newTables, newTableCount);
        return status;
    }

    PAGE_TABLE_ENTRY *ptEntry = &pt[PT_INDEX(virtAddr)];
    if ((*ptEntry & PTE_PRESENT) != 0)
    {
        KiRollbackNewTables(newTables, newTableCount);
        return EC_INVALID_STATE;
    }

    *ptEntry = (physAddr & PAGE_MASK) | (attributes & KE_PT_ALLOWED_LEAF_FLAGS) | PTE_PRESENT;

    if (KiReadCr3() == space->RootPageTablePhys)
        KiInvalidatePage(virtAddr);

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KePtUnmapPage(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr)
{
    if (!space)
        return EC_ILLEGAL_ARGUMENT;

    KE_PT_WALK walk;
    HO_STATUS status = KiWalkImportedRoot(space, virtAddr, &walk);
    if (status != EC_SUCCESS)
        return status;
    if (!walk.LeafEntry)
        return EC_INVALID_STATE;
    if (walk.Level != 1)
        return EC_NOT_SUPPORTED;

    *walk.LeafEntry = 0;

    if (KiReadCr3() == space->RootPageTablePhys)
        KiInvalidatePage(virtAddr);

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KePtProtectPage(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr, uint64_t attributes)
{
    if (!space)
        return EC_ILLEGAL_ARGUMENT;

    KE_PT_WALK walk;
    HO_STATUS status = KiWalkImportedRoot(space, virtAddr, &walk);
    if (status != EC_SUCCESS)
        return status;
    if (!walk.LeafEntry)
        return EC_INVALID_STATE;
    if (walk.Level != 1)
        return EC_NOT_SUPPORTED;

    uint64_t preserved = (walk.LeafValue & KE_PT_PHYS_ADDR_MASK) | (walk.LeafValue & (PTE_ACCESSED | PTE_DIRTY));
    *walk.LeafEntry = preserved | (attributes & KE_PT_ALLOWED_LEAF_FLAGS) | PTE_PRESENT;

    if (KiReadCr3() == space->RootPageTablePhys)
        KiInvalidatePage(virtAddr);

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KePtSelfTest(void)
{
    const KE_KERNEL_ADDRESS_SPACE *space = KeGetKernelAddressSpace();
    if (!space)
        return EC_INVALID_STATE;

    HO_VIRTUAL_ADDRESS scratchVirt;
    HO_STATUS status = KiFindScratchHole(space, &scratchVirt);
    if (status != EC_SUCCESS)
        return status;

    klog(KLOG_LEVEL_INFO, "[MM] PT HAL scratch candidate: va=%p\n", (void *)(uint64_t)scratchVirt);

    if (KeFindImportedRegion(space, scratchVirt) != NULL)
    {
        klog(KLOG_LEVEL_ERROR, "[MM] PT HAL scratch candidate still overlaps an imported region\n");
        return EC_INVALID_STATE;
    }

    HO_PHYSICAL_ADDRESS scratchPhys;
    status = KePmmAllocPages(1, NULL, &scratchPhys);
    if (status != EC_SUCCESS)
        return status;

    volatile uint64_t *hhdmAlias = (volatile uint64_t *)(uint64_t)HHDM_PHYS2VIRT(scratchPhys);
    *hhdmAlias = 0;

    KE_PT_MAPPING mapping;
    status = KePtQueryPage(space, scratchVirt, &mapping);
    if (status != EC_SUCCESS)
        goto cleanup_page;
    if (mapping.Present)
    {
        klog(KLOG_LEVEL_ERROR,
             "[MM] PT HAL scratch candidate unexpectedly mapped before self-test: level=%u attrs=%p\n", mapping.Level,
             mapping.Attributes);
        status = EC_INVALID_STATE;
        goto cleanup_page;
    }

    status = KePtMapPage(space, scratchVirt, scratchPhys, PTE_WRITABLE | PTE_NO_EXECUTE);
    if (status != EC_SUCCESS)
        goto cleanup_page;

    status = KePtQueryPage(space, scratchVirt, &mapping);
    if (status != EC_SUCCESS)
        goto cleanup_mapping;
    if (!mapping.Present || mapping.PhysicalBase != scratchPhys || mapping.Level != 1)
    {
        klog(KLOG_LEVEL_ERROR,
             "[MM] PT HAL scratch query mismatch after map: present=%u level=%u phys=%p expected=%p attrs=%p\n",
             mapping.Present, mapping.Level, (void *)(uint64_t)mapping.PhysicalBase, (void *)(uint64_t)scratchPhys,
             mapping.Attributes);
        status = EC_INVALID_STATE;
        goto cleanup_mapping;
    }

    volatile uint64_t *scratchAlias = (volatile uint64_t *)(uint64_t)scratchVirt;
    const uint64_t pattern = 0x4B41535054484C31ULL ^ scratchVirt ^ scratchPhys;
    *hhdmAlias = pattern;
    if (*scratchAlias != pattern)
    {
        klog(KLOG_LEVEL_ERROR, "[MM] PT HAL scratch alias mismatch after write: expected=%p actual=%p\n", pattern,
             *scratchAlias);
        status = EC_INVALID_STATE;
        goto cleanup_mapping;
    }

    status = KePtProtectPage(space, scratchVirt, PTE_NO_EXECUTE);
    if (status != EC_SUCCESS)
        goto cleanup_mapping;

    status = KePtQueryPage(space, scratchVirt, &mapping);
    if (status != EC_SUCCESS)
        goto cleanup_mapping;
    if (!mapping.Present || (mapping.Attributes & PTE_WRITABLE) != 0)
    {
        klog(KLOG_LEVEL_ERROR, "[MM] PT HAL scratch protect mismatch: present=%u attrs=%p\n", mapping.Present,
             mapping.Attributes);
        status = EC_INVALID_STATE;
        goto cleanup_mapping;
    }

    status = KePtUnmapPage(space, scratchVirt);
    if (status != EC_SUCCESS)
        goto cleanup_page;

    status = KePtQueryPage(space, scratchVirt, &mapping);
    if (status != EC_SUCCESS)
        goto cleanup_page;
    if (mapping.Present)
    {
        klog(KLOG_LEVEL_ERROR, "[MM] PT HAL scratch unmap mismatch: mapping still present attrs=%p\n",
             mapping.Attributes);
        status = EC_INVALID_STATE;
        goto cleanup_page;
    }

    klog(KLOG_LEVEL_INFO, "[MM] PT HAL scratch OK: va=%p phys=%p\n", (void *)(uint64_t)scratchVirt,
         (void *)(uint64_t)scratchPhys);
    status = EC_SUCCESS;

cleanup_page:
    (void)KePmmFreePages(scratchPhys, 1);
    return status;

cleanup_mapping: {
    HO_STATUS cleanupStatus = KePtUnmapPage(space, scratchVirt);
    if (cleanupStatus != EC_SUCCESS && cleanupStatus != EC_INVALID_STATE)
    {
        klog(KLOG_LEVEL_WARNING, "[MM] PT HAL scratch cleanup unmap failed: %k\n", cleanupStatus);
    }
}
    goto cleanup_page;
}
