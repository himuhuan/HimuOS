/**
 * HimuOperatingSystem
 *
 * File: blmm/capsule.c
 * Description: Boot capsule creation and initial mapping helpers.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "blmm_internal.h"

#define MAX_PAGE_TABLE_POOL_PAGES 256 // 1MB for page table
#define IA32_APIC_BASE_MSR        0x1BU
#define IA32_APIC_BASE_ADDR_MASK  0x00000000FFFFF000ULL

static EFI_STATUS RecordBootMappingRegion(BOOT_CAPSULE *capsule,
                                          BOOT_MAPPING_REGION_TYPE type,
                                          BOOT_MAPPING_PROVENANCE provenance,
                                          uint32_t provenanceValue,
                                          BOOT_MAPPING_LIFETIME lifetime,
                                          HO_PHYSICAL_ADDRESS physStart,
                                          HO_VIRTUAL_ADDRESS virtStart,
                                          uint64_t size,
                                          uint64_t attributes);
static BOOL IsVirtualRangeMapped(UINT64 pml4BasePhys, HO_VIRTUAL_ADDRESS virtStart, uint64_t size);

UINT64
GetCapsulePhysPages(const BOOT_CAPSULE_LAYOUT *layout, BOOT_CAPSULE_PAGE_LAYOUT *pageLayout)
{
    UINT64 handoffPages = HO_ALIGN_UP(BootCapsuleHandoffBytes(layout), PAGE_4KB) >> 12;
    UINT64 kernelPages = HO_ALIGN_UP(layout->KrnlCodeSize + layout->KrnlDataSize, PAGE_4KB) >> 12;
    UINT64 kernelStackPages = HO_ALIGN_UP(layout->KrnlStackSize, PAGE_4KB) >> 12;
    UINT64 ist1StackPages = HO_ALIGN_UP(layout->IST1StackSize, PAGE_4KB) >> 12;
    UINT64 totalPages = handoffPages + kernelPages + kernelStackPages + ist1StackPages;

    if (pageLayout != NULL)
    {
        pageLayout->HandoffPages = handoffPages;
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
    EFI_STATUS status = g_ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &basePhys);
    if (EFI_ERROR(status))
    {
        LOG_ERROR("Failed to allocate boot capsule: %k (0x%x)\r\n", status, status);
        return NULL;
    }
    status = BootValidateGuardedAllocation(L"boot capsule", basePhys, pages);
    if (EFI_ERROR(status))
    {
        (void)g_ST->BootServices->FreePages(basePhys, pages);
        return NULL;
    }
    LOG_DEBUG("Allocated %u pages for boot capsule at PA %p\r\n", pages, basePhys);

    BOOT_CAPSULE *capsule = (BOOT_CAPSULE *)basePhys;
    memset(capsule, 0, pages << 12);

    capsule->Magic = BOOT_CAPSULE_MAGIC;
    capsule->BasePhys = basePhys;
    capsule->ManifestPhys = basePhys + BootCapsuleManifestOffset(layout->HeaderSize);
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

    capsule->MemoryMapPhys = basePhys + BootCapsuleMemoryMapOffset(layout->HeaderSize, layout->ManifestSize);
    capsule->KrnlEntryPhys = basePhys + (pageLayout.HandoffPages << 12);
    capsule->KrnlStackPhys = capsule->KrnlEntryPhys + (pageLayout.KrnlPages << 12);
    capsule->KrnlIST1StackPhys = capsule->KrnlStackPhys + (pageLayout.KrnlStackPages << 12);

    status = BootMappingManifestInitialize(capsule);
    if (EFI_ERROR(status))
    {
        LOG_ERROR("Failed to initialize Boot Mapping Manifest: %k (0x%x)\r\n", status, status);
        (void)g_ST->BootServices->FreePages(basePhys, pages);
        return NULL;
    }

    return capsule;
}

UINT64
CreateInitialMapping(BOOT_CAPSULE *capsule, EFI_MEMORY_MAP *memoryMap)
{
    EFI_PHYSICAL_ADDRESS poolBase = 0;
    HOB_BALLOC pageTableAlloc;
    EFI_STATUS status;
    UINT64 nxFlag = GetNxFlag();
    UINT64 mappedDescCount = 0;
    UINT64 highestPhysExclusive = 0;

    do
    {
        if (!capsule || !memoryMap)
        {
            LOG_ERROR("CreateInitialMapping: invalid arguments\r\n");
            break;
        }

        status =
            g_ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, MAX_PAGE_TABLE_POOL_PAGES, &poolBase);
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

        status = BootValidateGuardedAllocation(L"page table pool", poolBase, MAX_PAGE_TABLE_POOL_PAGES);
        if (EFI_ERROR(status))
        {
            (void)g_ST->BootServices->FreePages(poolBase, MAX_PAGE_TABLE_POOL_PAGES);
            poolBase = 0;
            break;
        }

        capsule->PageTableInfo.Ptr = pml4Phys;
        capsule->PageTableInfo.Size = pageTableAlloc.TotalSize;

        if (HoNullDetectionEnabled())
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, PAGE_4KB, PAGE_4KB, PAGE_2MB - PAGE_4KB,
                               PTE_PRESENT | PTE_WRITABLE);
            if (!EFI_ERROR(status))
            {
                status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_IDENTITY, BOOT_MAPPING_PROVENANCE_STATIC,
                                                 0, BOOT_MAPPING_LIFETIME_TEMPORARY, PAGE_4KB, PAGE_4KB,
                                                 PAGE_2MB - PAGE_4KB, PTE_WRITABLE);
            }
            if (!EFI_ERROR(status))
            {
                status = MapRegion(&pageTableAlloc, pml4Phys, PAGE_2MB, PAGE_2MB, 0x80000000ULL - PAGE_2MB,
                                   PTE_PRESENT | PTE_WRITABLE);
            }
            if (!EFI_ERROR(status))
            {
                status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_IDENTITY, BOOT_MAPPING_PROVENANCE_STATIC,
                                                 0, BOOT_MAPPING_LIFETIME_TEMPORARY, PAGE_2MB, PAGE_2MB,
                                                 0x80000000ULL - PAGE_2MB, PTE_WRITABLE);
            }
        }
        else
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, 0, 0, 0x80000000ULL, PTE_PRESENT | PTE_WRITABLE);
            if (!EFI_ERROR(status))
            {
                status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_IDENTITY, BOOT_MAPPING_PROVENANCE_STATIC,
                                                 0, BOOT_MAPPING_LIFETIME_TEMPORARY, 0, 0, 0x80000000ULL,
                                                 PTE_WRITABLE);
            }
        }
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to map lower 2GB memory: %k (0x%x)\r\n", status, status);
            break;
        }

        status = MapFullHhdmFromMemoryMap(&pageTableAlloc, pml4Phys, capsule, memoryMap, nxFlag, &mappedDescCount,
                                          &highestPhysExclusive);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to map FULL HHDM: %k (0x%x)\r\n", status, status);
            break;
        }
        LOG_INFO("FULL HHDM mapped %u descriptors, highest PA=%p\r\n", mappedDescCount,
                 (void *)(UINTN)(highestPhysExclusive ? (highestPhysExclusive - 1ULL) : 0ULL));

        if (!IsVirtualRangeMapped(pml4Phys, HHDM_BASE_VA + capsule->PageTableInfo.Ptr, capsule->PageTableInfo.Size))
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->PageTableInfo.Ptr,
                               HHDM_BASE_VA + capsule->PageTableInfo.Ptr, capsule->PageTableInfo.Size,
                               PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to map page table pool at HHDM: %k (0x%x)\r\n", status, status);
                break;
            }
        }
        else
        {
            LOG_DEBUG("Page table pool already covered by existing HHDM mappings\r\n");
        }
        status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_BOOT_PAGE_TABLES,
                                         BOOT_MAPPING_PROVENANCE_STATIC, 0, BOOT_MAPPING_LIFETIME_RETAINED,
                                         capsule->PageTableInfo.Ptr, HHDM_BASE_VA + capsule->PageTableInfo.Ptr,
                                         capsule->PageTableInfo.Size, PTE_WRITABLE | nxFlag);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to record page table mapping in manifest: %k (0x%x)\r\n", status, status);
            break;
        }

        if (capsule->PageLayout.TotalPages > 0)
        {
            size_t capsuleSize = capsule->PageLayout.TotalPages << 12;
            if (!IsVirtualRangeMapped(pml4Phys, HHDM_BASE_VA + capsule->BasePhys, capsuleSize))
            {
                status = MapRegion(&pageTableAlloc, pml4Phys, capsule->BasePhys, HHDM_BASE_VA + capsule->BasePhys,
                                   capsuleSize, PTE_WRITABLE | nxFlag);
                if (EFI_ERROR(status))
                {
                    LOG_ERROR("Failed to map BOOT_CAPSULE at HHDM: %k (0x%x)\r\n", status, status);
                    break;
                }
            }
            else
            {
                LOG_DEBUG("Boot staging block already covered by existing HHDM mappings\r\n");
            }

            status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_BOOT_STAGING,
                                             BOOT_MAPPING_PROVENANCE_STATIC, 0, BOOT_MAPPING_LIFETIME_RETAINED,
                                             capsule->BasePhys, HHDM_BASE_VA + capsule->BasePhys, capsuleSize,
                                             PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to record boot staging mapping in manifest: %k (0x%x)\r\n", status, status);
                break;
            }

            status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_BOOT_HANDOFF,
                                             BOOT_MAPPING_PROVENANCE_STATIC, 0, BOOT_MAPPING_LIFETIME_RETAINED,
                                             capsule->BasePhys, HHDM_BASE_VA + capsule->BasePhys,
                                             capsule->PageLayout.HandoffPages << 12, PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to record boot handoff mapping in manifest: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        if (capsule->AcpiRsdpPhys != 0)
        {
            status = MapAcpiTables(&pageTableAlloc, pml4Phys, capsule, capsule->AcpiRsdpPhys);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to map ACPI tables at HHDM: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        if (capsule->Layout.KrnlCodeSize > 0)
        {
            UINT64 sizeAligned = HO_ALIGN_UP(capsule->Layout.KrnlCodeSize, PAGE_4KB);
            status =
                MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlEntryPhys, KRNL_BASE_VA, sizeAligned, PTE_WRITABLE);
            if (EFI_ERROR(status))
                break;

            status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_KERNEL_CODE,
                                             BOOT_MAPPING_PROVENANCE_STATIC, 0, BOOT_MAPPING_LIFETIME_KERNEL,
                                             capsule->KrnlEntryPhys, KRNL_BASE_VA, sizeAligned, PTE_WRITABLE);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to record kernel code mapping in manifest: %k (0x%x)\r\n", status, status);
                break;
            }
        }
        if (capsule->Layout.KrnlDataSize > 0)
        {
            UINT64 codeSizeAligned = HO_ALIGN_UP(capsule->Layout.KrnlCodeSize, PAGE_4KB);
            UINT64 dataSizeAligned = HO_ALIGN_UP(capsule->Layout.KrnlDataSize, PAGE_4KB);
            UINT64 dataPhys = capsule->KrnlEntryPhys + codeSizeAligned;
            UINT64 dataVirt = KRNL_BASE_VA + codeSizeAligned;
            status = MapRegion(&pageTableAlloc, pml4Phys, dataPhys, dataVirt, dataSizeAligned, PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
                break;

            status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_KERNEL_DATA,
                                             BOOT_MAPPING_PROVENANCE_STATIC, 0, BOOT_MAPPING_LIFETIME_KERNEL,
                                             dataPhys, dataVirt, dataSizeAligned, PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to record kernel data mapping in manifest: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        if (capsule->Layout.KrnlStackSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlStackPhys, KRNL_STACK_VA,
                               capsule->Layout.KrnlStackSize, PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
                break;

            status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_KERNEL_STACK,
                                             BOOT_MAPPING_PROVENANCE_STATIC, 0, BOOT_MAPPING_LIFETIME_KERNEL,
                                             capsule->KrnlStackPhys, KRNL_STACK_VA,
                                             HO_ALIGN_UP(capsule->Layout.KrnlStackSize, PAGE_4KB), PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to record kernel stack mapping in manifest: %k (0x%x)\r\n", status, status);
                break;
            }
        }
        if (capsule->Layout.IST1StackSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlIST1StackPhys, KRNL_IST1_STACK_VA,
                               capsule->Layout.IST1StackSize, PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
                break;

            status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_KERNEL_IST_STACK,
                                             BOOT_MAPPING_PROVENANCE_STATIC, 0, BOOT_MAPPING_LIFETIME_KERNEL,
                                             capsule->KrnlIST1StackPhys, KRNL_IST1_STACK_VA,
                                             HO_ALIGN_UP(capsule->Layout.IST1StackSize, PAGE_4KB),
                                             PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to record IST1 stack mapping in manifest: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        if (capsule->FramebufferSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->FramebufferPhys, MMIO_BASE_VA,
                               capsule->FramebufferSize, PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
                break;

            status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_FRAMEBUFFER,
                                             BOOT_MAPPING_PROVENANCE_STATIC, 0, BOOT_MAPPING_LIFETIME_DEVICE,
                                             capsule->FramebufferPhys, MMIO_BASE_VA,
                                             HO_ALIGN_UP(capsule->FramebufferSize, PAGE_4KB),
                                             PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to record framebuffer mapping in manifest: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        UINT64 apicBaseMsr = rdmsr(IA32_APIC_BASE_MSR);
        UINT64 lapicBasePhys = apicBaseMsr & IA32_APIC_BASE_ADDR_MASK;
        if (lapicBasePhys != 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, lapicBasePhys, HHDM_BASE_VA + lapicBasePhys, PAGE_4KB,
                               PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to map LAPIC MMIO at %p\r\n", lapicBasePhys);
                break;
            }

            status = RecordBootMappingRegion(capsule, BOOT_MAPPING_REGION_LAPIC_MMIO,
                                             BOOT_MAPPING_PROVENANCE_CPU_MSR, IA32_APIC_BASE_MSR,
                                             BOOT_MAPPING_LIFETIME_DEVICE, lapicBasePhys,
                                             HHDM_BASE_VA + lapicBasePhys, PAGE_4KB,
                                             PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to record LAPIC mapping in manifest: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        LOG_DEBUG("Used %u KB of %u KB for page tables\r\n", pageTableAlloc.Offset >> 12,
                  pageTableAlloc.TotalSize >> 12);
        DumpBootMappingManifestSummary(capsule);
        return HobRemaining(&pageTableAlloc);
    } while (FALSE);

    if (poolBase)
        g_ST->BootServices->FreePages(poolBase, MAX_PAGE_TABLE_POOL_PAGES);
    return 0;
}

static EFI_STATUS
RecordBootMappingRegion(BOOT_CAPSULE *capsule,
                        BOOT_MAPPING_REGION_TYPE type,
                        BOOT_MAPPING_PROVENANCE provenance,
                        uint32_t provenanceValue,
                        BOOT_MAPPING_LIFETIME lifetime,
                        HO_PHYSICAL_ADDRESS physStart,
                        HO_VIRTUAL_ADDRESS virtStart,
                        uint64_t size,
                        uint64_t attributes)
{
    if (size == 0)
        return EFI_SUCCESS;

    BOOT_MAPPING_RECORD_PARAMS params;
    memset(&params, 0, sizeof(params));
    params.VirtualStart = virtStart;
    params.PhysicalStart = physStart;
    params.Size = size;
    params.Attributes = attributes;
    params.ProvenanceValue = provenanceValue;
    params.Type = type;
    params.Provenance = provenance;
    params.Lifetime = lifetime;
    params.Granularity = DetectBootMappingGranularity(physStart, virtStart, size);
    return BootMappingManifestRecord(capsule, &params);
}

static BOOL
IsVirtualRangeMapped(UINT64 pml4BasePhys, HO_VIRTUAL_ADDRESS virtStart, uint64_t size)
{
    uint64_t offset = 0;
    PAGE_TABLE_ENTRY *pml4 = (PAGE_TABLE_ENTRY *)pml4BasePhys;

    if (size == 0)
        return TRUE;

    while (offset < size)
    {
        uint64_t virt = virtStart + offset;
        uint64_t covered = 0;
        uint64_t pml4Index = PML4_INDEX(virt);
        if ((pml4[pml4Index] & PTE_PRESENT) == 0)
            return FALSE;

        PAGE_TABLE_ENTRY *pdpt = (PAGE_TABLE_ENTRY *)(pml4[pml4Index] & PAGE_MASK);
        uint64_t pdptIndex = PDPT_INDEX(virt);
        if ((pdpt[pdptIndex] & PTE_PRESENT) == 0)
            return FALSE;

        if (pdpt[pdptIndex] & PTE_PAGE_SIZE)
        {
            covered = PAGE_1GB - (virt & (PAGE_1GB - 1));
        }
        else
        {
            PAGE_TABLE_ENTRY *pd = (PAGE_TABLE_ENTRY *)(pdpt[pdptIndex] & PAGE_MASK);
            uint64_t pdIndex = PD_INDEX(virt);
            if ((pd[pdIndex] & PTE_PRESENT) == 0)
                return FALSE;

            if (pd[pdIndex] & PTE_PAGE_SIZE)
            {
                covered = PAGE_2MB - (virt & (PAGE_2MB - 1));
            }
            else
            {
                PAGE_TABLE_ENTRY *pt = (PAGE_TABLE_ENTRY *)(pd[pdIndex] & PAGE_MASK);
                uint64_t ptIndex = PT_INDEX(virt);
                if ((pt[ptIndex] & PTE_PRESENT) == 0)
                    return FALSE;

                covered = PAGE_4KB - (virt & (PAGE_4KB - 1));
            }
        }

        if (covered == 0)
            return FALSE;

        if (covered > size - offset)
            covered = size - offset;

        offset += covered;
    }

    return TRUE;
}
