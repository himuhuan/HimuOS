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

        if (HoNullDetectionEnabled())
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, PAGE_4KB, PAGE_4KB, PAGE_2MB - PAGE_4KB,
                               PTE_PRESENT | PTE_WRITABLE);
            if (!EFI_ERROR(status))
            {
                status = MapRegion(&pageTableAlloc, pml4Phys, PAGE_2MB, PAGE_2MB, 0x80000000ULL - PAGE_2MB,
                                   PTE_PRESENT | PTE_WRITABLE);
            }
        }
        else
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, 0, 0, 0x80000000ULL, PTE_PRESENT | PTE_WRITABLE);
        }
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to map lower 2GB memory: %k (0x%x)\r\n", status, status);
            break;
        }

        status = MapFullHhdmFromMemoryMap(&pageTableAlloc, pml4Phys, memoryMap, nxFlag, &mappedDescCount,
                                          &highestPhysExclusive);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to map FULL HHDM: %k (0x%x)\r\n", status, status);
            break;
        }
        LOG_INFO("FULL HHDM mapped %u descriptors, highest PA=%p\r\n", mappedDescCount,
                 (void *)(UINTN)(highestPhysExclusive ? (highestPhysExclusive - 1ULL) : 0ULL));

        if (capsule->PageLayout.TotalPages > 0)
        {
            size_t capsuleSize = capsule->PageLayout.TotalPages << 12;
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->BasePhys, HHDM_BASE_VA + capsule->BasePhys,
                               capsuleSize, PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to map BOOT_CAPSULE at HHDM: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        if (capsule->AcpiRsdpPhys != 0)
        {
            status = MapAcpiTables(&pageTableAlloc, pml4Phys, capsule->AcpiRsdpPhys);
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
        }

        if (capsule->Layout.KrnlStackSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlStackPhys, KRNL_STACK_VA,
                               capsule->Layout.KrnlStackSize, PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
                break;
        }
        if (capsule->Layout.IST1StackSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlIST1StackPhys, KRNL_IST1_STACK_VA,
                               capsule->Layout.IST1StackSize, PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
                break;
        }

        if (capsule->FramebufferSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->FramebufferPhys, MMIO_BASE_VA,
                               capsule->FramebufferSize, PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag);
            if (EFI_ERROR(status))
                break;
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
        }

        capsule->PageTableInfo.Ptr = pml4Phys;
        capsule->PageTableInfo.Size = pageTableAlloc.TotalSize;

        LOG_DEBUG("Used %u KB of %u KB for page tables\r\n", pageTableAlloc.Offset >> 12,
                  pageTableAlloc.TotalSize >> 12);
        return HobRemaining(&pageTableAlloc);
    } while (FALSE);

    if (poolBase)
        g_ST->BootServices->FreePages(poolBase, MAX_PAGE_TABLE_POOL_PAGES);
    return 0;
}
