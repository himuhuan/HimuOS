/**
 * HimuOperatingSystem
 *
 * File: blmm.c
 * Description: Bootloader memory management — orchestration, capsule construction, memory map.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "blmm_internal.h"

#define MIN_MEMMAP_PAGES          3   // 12KB for memory map at least
#define MAX_PAGE_TABLE_POOL_PAGES 256 // 1MB for page table
#define IA32_APIC_BASE_MSR        0x1BU
#define IA32_APIC_BASE_ADDR_MASK  0x00000000FFFFF000ULL

static EFI_MEMORY_MAP *InitMemoryMap(void *base, size_t size);
static EFI_STATUS FillMemoryMap(EFI_MEMORY_MAP *map);

EFI_MEMORY_MAP *
GetLoaderRuntimeMemoryMap(UINTN *allocatedPagesOut)
{
    EFI_PHYSICAL_ADDRESS mapBuffer = 0;
    EFI_MEMORY_MAP *map = NULL;
    UINTN mapBufferPages = MIN_MEMMAP_PAGES;
    UINTN allocatedPages = 0;

    if (allocatedPagesOut != NULL)
        *allocatedPagesOut = 0;

    while (TRUE)
    {
        UINT64 mapBufferSize = mapBufferPages << 12;
        EFI_STATUS status;

        if (mapBuffer != 0)
        {
            g_ST->BootServices->FreePages(mapBuffer, allocatedPages);
            mapBuffer = 0;
            allocatedPages = 0;
        }

        status = BootAllocateLoaderPages(mapBufferPages, &mapBuffer);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to allocate memory map buffer: %k (0x%x)\r\n", status, status);
            return NULL;
        }

        allocatedPages = mapBufferPages;
        map = InitMemoryMap((void *)mapBuffer, mapBufferSize);
        status = FillMemoryMap(map);
        if (!EFI_ERROR(status))
        {
            LOG_DEBUG("Allocated %u bytes for memory map buffer at PA %p\r\n", mapBufferSize, mapBuffer);
            if (allocatedPagesOut != NULL)
                *allocatedPagesOut = allocatedPages;
            return map;
        }

        if (EFI_STATUS_CODE_LOW(status) != EFI_BUFFER_TOO_SMALL)
        {
            LOG_ERROR("Failed to populate memory map buffer: %k (0x%x)\r\n", status, status);
            g_ST->BootServices->FreePages(mapBuffer, allocatedPages);
            return NULL;
        }

        mapBufferPages++;
    }
}

EFI_STATUS
LoadMemoryMap(HO_VIRTUAL_ADDRESS mapBasePhys, UINT64 maxSize, OUT UINTN *memoryMapKey)
{
    EFI_MEMORY_MAP *map = InitMemoryMap((void *)mapBasePhys, maxSize);
    EFI_STATUS status = FillMemoryMap(map);
    if (status != EFI_SUCCESS)
        return status;
    *memoryMapKey = map->MemoryMapKey;
    return EFI_SUCCESS;
}

UINT64
GetCapsulePhysPages(const BOOT_CAPSULE_LAYOUT *layout, BOOT_CAPSULE_PAGE_LAYOUT *pageLayout)
{
    UINT64 headerWithMapPages = HO_ALIGN_UP(layout->HeaderSize + layout->MemoryMapSize, PAGE_4KB) >> 12;
    UINT64 kernelPages = HO_ALIGN_UP(layout->KrnlCodeSize + layout->KrnlDataSize, PAGE_4KB) >> 12;
    UINT64 kernelStackPages = HO_ALIGN_UP(layout->KrnlStackSize, PAGE_4KB) >> 12;
    UINT64 ist1StackPages = HO_ALIGN_UP(layout->IST1StackSize, PAGE_4KB) >> 12;
    UINT64 totalPages = headerWithMapPages + kernelPages + kernelStackPages + ist1StackPages;

    if (pageLayout != NULL)
    {
        pageLayout->HeaderWithMapPages = headerWithMapPages;
        pageLayout->KrnlPages = kernelPages;
        pageLayout->KrnlStackPages = kernelStackPages;
        pageLayout->IST1StackPages = ist1StackPages;
        pageLayout->TotalPages = totalPages;
    }

    return totalPages;
}

BOOT_CAPSULE *
CreateCapsule(const BOOT_CAPSULE_LAYOUT *layout)
{
    BOOT_CAPSULE_PAGE_LAYOUT pageLayout;
    UINT64 pages = GetCapsulePhysPages(layout, &pageLayout);
    HO_PHYSICAL_ADDRESS basePhys = 0;
    EFI_STATUS status = BootAllocateLoaderPages(pages, &basePhys);
    if (EFI_ERROR(status))
    {
        LOG_ERROR("Failed to allocate boot capsule: %k (0x%x)\r\n", status, status);
        return NULL;
    }
    LOG_DEBUG("Allocated %u pages for boot capsule at PA %p\r\n", pages, basePhys);

    BOOT_CAPSULE *capsule = (BOOT_CAPSULE *)basePhys;
    memset(capsule, 0, pages << 12);

    capsule->Magic = BOOT_CAPSULE_MAGIC;
    capsule->BasePhys = basePhys;
    capsule->VideoModeType = VIDEO_MODE_TYPE_UEFI;
    capsule->PixelFormat =
        (g_GOP->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) ? PIXEL_FORMAT_BGR : PIXEL_FORMAT_RGB;
    capsule->FramebufferPhys = g_GOP->Mode->FrameBufferBase;
    capsule->FramebufferSize = g_GOP->Mode->FrameBufferSize;
    capsule->HorizontalResolution = g_GOP->Mode->Info->HorizontalResolution;
    capsule->VerticalResolution = g_GOP->Mode->Info->VerticalResolution;
    capsule->PixelsPerScanLine = g_GOP->Mode->Info->PixelsPerScanLine;

    memcpy(&capsule->Layout, layout, sizeof(BOOT_CAPSULE_LAYOUT));
    memcpy(&capsule->PageLayout, &pageLayout, sizeof(BOOT_CAPSULE_PAGE_LAYOUT));

    capsule->MemoryMapPhys = basePhys + layout->HeaderSize;
    capsule->KrnlEntryPhys = basePhys + (pageLayout.HeaderWithMapPages << 12);
    capsule->KrnlStackPhys = capsule->KrnlEntryPhys + (pageLayout.KrnlPages << 12);
    capsule->KrnlIST1StackPhys = capsule->KrnlStackPhys + (pageLayout.KrnlStackPages << 12);

    UINT64 manifestBytes = (layout->HeaderSize > sizeof(BOOT_CAPSULE)) ? (layout->HeaderSize - sizeof(BOOT_CAPSULE)) : 0;
    capsule->MappingManifest.Magic = BOOT_MAPPING_MANIFEST_MAGIC;
    capsule->MappingManifest.Version = BOOT_MAPPING_MANIFEST_VERSION;
    capsule->MappingManifest.EntrySize = sizeof(BOOT_MAPPING_MANIFEST_ENTRY);
    capsule->MappingManifest.EntryCount = 0;
    capsule->MappingManifest.EntryCapacity = (UINT32)(manifestBytes / sizeof(BOOT_MAPPING_MANIFEST_ENTRY));
    capsule->MappingManifest.RequiredCategories = 0;
    capsule->MappingManifest.EntriesPhys = (manifestBytes != 0) ? (basePhys + sizeof(BOOT_CAPSULE)) : 0;
    capsule->MappingManifest.EntriesBytes = BootMappingManifestStorageBytes(capsule->MappingManifest.EntryCapacity);
    LOG_DEBUG("Reserved boot mapping manifest: capacity=%u entry_size=%u bytes=%u\r\n",
              capsule->MappingManifest.EntryCapacity, sizeof(BOOT_MAPPING_MANIFEST_ENTRY),
              capsule->MappingManifest.EntriesBytes);
    return capsule;
}

UINT64
CreateInitialMapping(BOOT_CAPSULE *capsule, EFI_MEMORY_MAP *memoryMap)
{
    EFI_PHYSICAL_ADDRESS poolBase = 0;
    HOB_BALLOC pageTableAlloc;
    EFI_STATUS status;
    UINT64 nxFlag = BlGetNxFlag();
    UINT64 mappedDescCount = 0;
    UINT64 highestPhysExclusive = 0;

    do
    {
        LOG_INFO("Creating initial page tables and mappings\r\n");
        
        if (!capsule || !memoryMap)
        {
            LOG_ERROR("CreateInitialMapping: invalid arguments\r\n");
            break;
        }

        status = BootAllocateLoaderPages(MAX_PAGE_TABLE_POOL_PAGES, &poolBase);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to allocate page table pool: %k (0x%x)\r\n", status, status);
            break;
        }
        status = HobAllocCreate(poolBase, &pageTableAlloc, MAX_PAGE_TABLE_POOL_PAGES);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to create page table allocator: %k (0x%x)\r\n", status, status);
            break;
        }
        LOG_DEBUG("Allocated %u pages for page table pool at PA %p\r\n", MAX_PAGE_TABLE_POOL_PAGES, poolBase);

        HO_PHYSICAL_ADDRESS pml4Phys = (HO_PHYSICAL_ADDRESS)HobAlloc(&pageTableAlloc, PAGE_4KB, PAGE_4KB);
        if (pml4Phys == 0)
        {
            LOG_ERROR("Failed to allocate PML4 table: %k\r\n", EFI_OUT_OF_RESOURCES);
            break;
        }

        capsule->MappingManifest.RequiredCategories = BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_BOOTSTRAP_IDENTITY) |
                                                     BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_HHDM) |
                                                     BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_BOOT_CAPSULE) |
                                                     BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_KERNEL_TEXT) |
                                                     BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_BOOT_STACK) |
                                                     BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_IST_STACK) |
                                                     BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_PAGE_TABLES);
        if (capsule->Layout.KrnlDataSize > 0)
            capsule->MappingManifest.RequiredCategories |= BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_KERNEL_DATA);
        if (capsule->FramebufferSize > 0)
            capsule->MappingManifest.RequiredCategories |= BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_FRAMEBUFFER);
        if (capsule->AcpiRsdpPhys != 0)
            capsule->MappingManifest.RequiredCategories |= BootMappingCategoryMask(BOOT_MAPPING_CATEGORY_ACPI);

        status = BlMapInitialIdentityWindow(&pageTableAlloc, pml4Phys, capsule, 0x80000000ULL, PTE_PRESENT | PTE_WRITABLE);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to map lower 2GB memory: %k (0x%x)\r\n", status, status);
            break;
        }

        // B. FULL HHDM from current EFI memory map
        status = BlMapFullHhdmFromMemoryMap(&pageTableAlloc, pml4Phys, capsule, memoryMap, nxFlag, &mappedDescCount,
                                            &highestPhysExclusive);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to map FULL HHDM: %k (0x%x)\r\n", status, status);
            break;
        }
        LOG_INFO("FULL HHDM mapped %u descriptors, highest PA=%p\r\n", mappedDescCount,
                 (void *)(UINTN)(highestPhysExclusive ? (highestPhysExclusive - 1ULL) : 0ULL));

        // B. BOOT_CAPSULE @ HHDM
        if (capsule->PageLayout.TotalPages > 0)
        {
            size_t capsuleSize = capsule->PageLayout.TotalPages << 12;
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->BasePhys, HHDM_BASE_VA + capsule->BasePhys,
                               capsuleSize, PTE_WRITABLE | nxFlag, capsule, BOOT_MAPPING_CATEGORY_BOOT_CAPSULE,
                               BOOT_MAPPING_ATTR_BOOT_IMPORTED);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to map BOOT_CAPSULE at HHDM: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        // C. ACPI tables @ HHDM
        if (capsule->AcpiRsdpPhys != 0)
        {
            status = BlMapAcpiTables(&pageTableAlloc, pml4Phys, capsule, capsule->AcpiRsdpPhys);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to map ACPI tables at HHDM: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        // D. Kernel code and data segments
        if (capsule->Layout.KrnlCodeSize > 0)
        {
            UINT64 sizeAligned = HO_ALIGN_UP(capsule->Layout.KrnlCodeSize, PAGE_4KB);
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlEntryPhys, KRNL_BASE_VA, sizeAligned,
                               PTE_WRITABLE, capsule, BOOT_MAPPING_CATEGORY_KERNEL_TEXT,
                               BOOT_MAPPING_ATTR_BOOT_IMPORTED);
            if (EFI_ERROR(status))
                break;
        }
        if (capsule->Layout.KrnlDataSize > 0)
        {
            UINT64 codeSizeAligned = HO_ALIGN_UP(capsule->Layout.KrnlCodeSize, PAGE_4KB);
            UINT64 dataSizeAligned = HO_ALIGN_UP(capsule->Layout.KrnlDataSize, PAGE_4KB);
            UINT64 dataPhys = capsule->KrnlEntryPhys + codeSizeAligned;
            UINT64 dataVirt = KRNL_BASE_VA + codeSizeAligned;
            status = MapRegion(&pageTableAlloc, pml4Phys, dataPhys, dataVirt, dataSizeAligned, PTE_WRITABLE | nxFlag,
                               capsule, BOOT_MAPPING_CATEGORY_KERNEL_DATA, BOOT_MAPPING_ATTR_BOOT_IMPORTED);
            if (EFI_ERROR(status))
                break;
        }

        // E. Kernel stack
        if (capsule->Layout.KrnlStackSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlStackPhys, KRNL_STACK_VA,
                               capsule->Layout.KrnlStackSize, PTE_WRITABLE | nxFlag, capsule,
                               BOOT_MAPPING_CATEGORY_BOOT_STACK,
                               BOOT_MAPPING_ATTR_BOOT_IMPORTED | BOOT_MAPPING_ATTR_MIGRATABLE);
            if (EFI_ERROR(status))
                break;
        }
        if (capsule->Layout.IST1StackSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlIST1StackPhys, KRNL_IST1_STACK_VA,
                               capsule->Layout.IST1StackSize, PTE_WRITABLE | nxFlag, capsule,
                               BOOT_MAPPING_CATEGORY_IST_STACK,
                               BOOT_MAPPING_ATTR_BOOT_IMPORTED | BOOT_MAPPING_ATTR_MIGRATABLE);
            if (EFI_ERROR(status))
                break;
        }

        // F. Framebuffer MMIO
        if (capsule->FramebufferSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->FramebufferPhys, MMIO_BASE_VA,
                               capsule->FramebufferSize, PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag, capsule,
                               BOOT_MAPPING_CATEGORY_FRAMEBUFFER, BOOT_MAPPING_ATTR_BOOT_IMPORTED);
            if (EFI_ERROR(status))
                break;
        }

        // G. Local APIC MMIO @ HHDM
        UINT64 apicBaseMsr = rdmsr(IA32_APIC_BASE_MSR);
        UINT64 lapicBasePhys = apicBaseMsr & IA32_APIC_BASE_ADDR_MASK;
        if (lapicBasePhys != 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, lapicBasePhys, HHDM_BASE_VA + lapicBasePhys, PAGE_4KB,
                               PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag, capsule, BOOT_MAPPING_CATEGORY_LAPIC_MMIO,
                               BOOT_MAPPING_ATTR_BOOT_IMPORTED);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to map LAPIC MMIO at %p\r\n", lapicBasePhys);
                break;
            }
        }

        capsule->PageTableInfo.Ptr = pml4Phys;
        capsule->PageTableInfo.Size = pageTableAlloc.TotalSize;

        BOOT_MAPPING_MANIFEST_ENTRY ptEntry;
        memset(&ptEntry, 0, sizeof(ptEntry));
        ptEntry.Category = BOOT_MAPPING_CATEGORY_PAGE_TABLES;
        ptEntry.CacheType = BlGetBootMappingCacheType(PTE_WRITABLE | nxFlag);
        ptEntry.Attributes = BlGetBootMappingEntryAttributes(PTE_WRITABLE | nxFlag, BOOT_MAPPING_ATTR_BOOT_IMPORTED);
        ptEntry.VirtStart = HHDM_BASE_VA + pml4Phys;
        ptEntry.VirtSize = pageTableAlloc.TotalSize;
        ptEntry.PhysStart = pml4Phys;
        ptEntry.PhysSize = pageTableAlloc.TotalSize;
        ptEntry.PageSize = PAGE_4KB;
        ptEntry.RawPteFlags = PTE_WRITABLE | nxFlag;
        status = BlAppendBootMappingEntry(capsule, &ptEntry);
        if (EFI_ERROR(status))
            break;

        BlSortBootMappingManifest(capsule);
        status = BlValidateBootMappingManifest(capsule);
        if (EFI_ERROR(status))
            break;

        LOG_DEBUG("Used %u KB of %u KB for page tables\r\n", pageTableAlloc.Offset >> 12,
                  pageTableAlloc.TotalSize >> 12);
        return HobRemaining(&pageTableAlloc);
    } while (FALSE);
    if (poolBase)
        g_ST->BootServices->FreePages(poolBase, MAX_PAGE_TABLE_POOL_PAGES);
    return 0;
}

static EFI_MEMORY_MAP *
InitMemoryMap(void *base, size_t size)
{
    EFI_MEMORY_MAP *map = (EFI_MEMORY_MAP *)base;
    memset(base, 0, size);

    map->Size = size;
    map->DescriptorTotalSize = size - sizeof(EFI_MEMORY_MAP);
    return map;
}

static EFI_STATUS
FillMemoryMap(EFI_MEMORY_MAP *map)
{
    uint64_t expectedMapSize = 0;

    EFI_STATUS status = g_ST->BootServices->GetMemoryMap(&expectedMapSize, NULL, &map->MemoryMapKey,
                                                         &map->DescriptorSize, &map->DescriptorVersion);
    if (EFI_STATUS_CODE_LOW(status) != EFI_BUFFER_TOO_SMALL)
    {
        LOG_ERROR(L"Failed to get memory_map_size: %k (0x%x)\r\n", status, status);
        return status;
    }
    expectedMapSize = HO_ALIGN_UP(expectedMapSize + 32 * map->DescriptorSize, PAGE_4KB);
    if (expectedMapSize > map->DescriptorTotalSize)
        return EFI_BUFFER_TOO_SMALL;

    status = g_ST->BootServices->GetMemoryMap(&map->DescriptorTotalSize, map->Segs, &map->MemoryMapKey,
                                              &map->DescriptorSize, &map->DescriptorVersion);
    if (EFI_ERROR(status))
        return status;

    map->DescriptorCount = (map->DescriptorSize == 0) ? 0 : (map->DescriptorTotalSize / map->DescriptorSize);
    return EFI_SUCCESS;
}
