#include "blmm.h"
#include "bootloader.h"
#include "arch/amd64/pm.h"
#include "arch/amd64/acpi.h"
#include "ho_balloc.h"
#include "io.h"

#define MIN_MEMMAP_PAGES          3  // 12KB for memory map at least
#define MAX_PAGE_TABLE_POOL_PAGES 64 // 512KB for page table

static EFI_MEMORY_MAP *InitMemoryMap(void *base, size_t size);
static EFI_STATUS FillMemoryMap(EFI_MEMORY_MAP *map);
static EFI_STATUS MapAcpiTables(HOB_BALLOC *allocator, UINT64 pml4BasePhys, HO_PHYSICAL_ADDRESS rsdpPhys);
static EFI_STATUS MapAcpiRange(HOB_BALLOC *allocator, UINT64 pml4BasePhys, UINT64 tablePhys, UINT32 length);

EFI_MEMORY_MAP *
GetLoaderRuntimeMemoryMap()
{
    EFI_PHYSICAL_ADDRESS mapBuffer = 0;
    EFI_STATUS status = !EFI_SUCCESS;
    EFI_MEMORY_MAP *map = NULL;
    int mapBufferPages = MIN_MEMMAP_PAGES;

    while (status != EFI_SUCCESS)
    {
        uint64_t mapBufferSize = mapBufferPages << 12;
        if (mapBuffer != 0)
        {
            g_ST->BootServices->FreePages(mapBuffer, mapBufferPages);
            mapBuffer = 0;
        }

        status = g_ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, mapBufferPages, &mapBuffer);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to allocate memory map buffer: %k (0x%x)\r\n", status, status);
            return NULL;
        }
        map = InitMemoryMap((void *)mapBuffer, mapBufferSize);
        status = FillMemoryMap(map);
        if (!EFI_ERROR(status))
            LOG_DEBUG("Allocated %u bytes for memory map buffer at PA %p\r\n", mapBufferSize, mapBuffer);
        mapBufferPages++;
    }

    return map;
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
    HO_PHYSICAL_ADDRESS basePhys;
    EFI_STATUS status = g_ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &basePhys);
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
    return capsule;
}

UINT64
CreateInitialMapping(BOOT_CAPSULE *capsule)
{
    EFI_PHYSICAL_ADDRESS poolBase = 0;
    HOB_BALLOC pageTableAlloc;
    EFI_STATUS status;

    do
    {
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

        status = MapRegion(&pageTableAlloc, pml4Phys, 0, 0, 0x80000000ULL, PTE_PRESENT | PTE_WRITABLE);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Failed to map lower 2GB memory: %k (0x%x)\r\n", status, status);
            break;
        }

        // B. BOOT_CAPSULE @ HHDM
        if (capsule->PageLayout.TotalPages > 0)
        {
            size_t capsuleSize = capsule->PageLayout.TotalPages << 12;
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->BasePhys, HHDM_BASE_VA + capsule->BasePhys,
                               capsuleSize, PTE_WRITABLE | PTE_NO_EXECUTE);
            if (EFI_ERROR(status))
            {
                LOG_ERROR("Failed to map BOOT_CAPSULE at HHDM: %k (0x%x)\r\n", status, status);
                break;
            }
        }

        // C. ACPI tables @ HHDM
        if (capsule->AcpiRsdpPhys != 0)
        {
            status = MapAcpiTables(&pageTableAlloc, pml4Phys, capsule->AcpiRsdpPhys);
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
            status = MapRegion(&pageTableAlloc, pml4Phys, dataPhys, dataVirt, dataSizeAligned,
                               PTE_WRITABLE | PTE_NO_EXECUTE);
            if (EFI_ERROR(status))
                break;
        }

        // E. Kernel stack
        if (capsule->Layout.KrnlStackSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlStackPhys, KRNL_STACK_VA,
                               capsule->Layout.KrnlStackSize, PTE_WRITABLE | PTE_NO_EXECUTE);
            if (EFI_ERROR(status))
                break;
        }
        if (capsule->Layout.IST1StackSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->KrnlIST1StackPhys, KRNL_IST1_STACK_VA,
                               capsule->Layout.IST1StackSize, PTE_WRITABLE | PTE_NO_EXECUTE);
            if (EFI_ERROR(status))
                break;
        }

        // F. Framebuffer MMIO
        if (capsule->FramebufferSize > 0)
        {
            status = MapRegion(&pageTableAlloc, pml4Phys, capsule->FramebufferPhys, MMIO_BASE_VA,
                               capsule->FramebufferSize, PTE_CACHE_DISABLE | PTE_WRITABLE | PTE_NO_EXECUTE);
            if (EFI_ERROR(status))
                break;
        }

        capsule->PageTableInfo.Ptr = pml4Phys;
        // All right...it's just a temporary page table, so we always recycle the whole pool size
        capsule->PageTableInfo.Size = pageTableAlloc.TotalSize;

        LOG_DEBUG("Used %u KB of %u KB for page tables\r\n", pageTableAlloc.Offset >> 12,
                  pageTableAlloc.TotalSize >> 12);
        return HobRemaining(&pageTableAlloc);
    } while (FALSE);
    if (poolBase)
        g_ST->BootServices->FreePages(poolBase, MAX_PAGE_TABLE_POOL_PAGES);
    return 0;
}

EFI_STATUS
MapPage(MAP_PAGE_PARAMS *params)
{
    if (params->PageSize != PAGE_4KB && params->PageSize != PAGE_2MB && params->PageSize != PAGE_1GB)
    {
        LOG_ERROR(L"Unsupported page size: %u\r\n", params->PageSize);
        return EFI_INVALID_PARAMETER;
    }

    UINT64 alignedAddrPhys = HO_ALIGN_DOWN(params->AddrPhys, params->PageSize);
    UINT64 alignedAddrVirt = HO_ALIGN_DOWN(params->AddrVirt, params->PageSize);
    HOB_BALLOC *allocator = params->BumpAllocator;
    PAGE_TABLE_ENTRY *pml4 = (PAGE_TABLE_ENTRY *)params->TableBasePhys;
    UINT64 flags = (params->PageSize != PAGE_4KB) ? (params->Flags | PTE_PAGE_SIZE) : params->Flags;
    UINT64 isPresent = (HO_LIKELY(!params->NotPresent)) ? PTE_PRESENT : 0;

    UINT64 pml4Index = PML4_INDEX(alignedAddrVirt);
    if (!(pml4[pml4Index] & PTE_PRESENT))
    {
        UINT64 pdptPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (pdptPhys == 0)
        {
            LOG_ERROR(L"Failed to allocate PDPT: %k\r\n", EFI_OUT_OF_RESOURCES);
            return EFI_OUT_OF_RESOURCES;
        }
        pml4[pml4Index] = pdptPhys | PTE_PRESENT | PTE_WRITABLE;
    }

    PAGE_TABLE_ENTRY *pdpt = (PAGE_TABLE_ENTRY *)(pml4[pml4Index] & PAGE_MASK);
    UINT64 pdptIndex = PDPT_INDEX(alignedAddrVirt);
    if (params->PageSize == PAGE_1GB)
    {
        if (pdpt[pdptIndex] & PTE_PRESENT)
        {
            LOG_ERROR(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
        pdpt[pdptIndex] = alignedAddrPhys | flags | isPresent;
        return EFI_SUCCESS;
    }

    if (!(pdpt[pdptIndex] & PTE_PRESENT))
    {
        UINT64 pdPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (pdPhys == 0)
        {
            LOG_ERROR(L"Failed to allocate PD: %k\r\n", EFI_OUT_OF_RESOURCES);
            return EFI_OUT_OF_RESOURCES;
        }
        pdpt[pdptIndex] = pdPhys | PTE_PRESENT | PTE_WRITABLE;
    }

    PAGE_TABLE_ENTRY *pd = (PAGE_TABLE_ENTRY *)(pdpt[pdptIndex] & PAGE_MASK);
    UINT64 pdIndex = PD_INDEX(alignedAddrVirt);
    if (params->PageSize == PAGE_2MB)
    {
        if (pd[pdIndex] & PTE_PRESENT)
        {
            LOG_ERROR(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
        pd[pdIndex] = alignedAddrPhys | flags | isPresent;
        return EFI_SUCCESS;
    }

    if (!(pd[pdIndex] & PTE_PRESENT))
    {
        UINT64 ptPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (ptPhys == 0)
        {
            LOG_ERROR(L"Failed to allocate PT: %k\r\n", EFI_OUT_OF_RESOURCES);
            return EFI_OUT_OF_RESOURCES;
        }
        pd[pdIndex] = ptPhys | PTE_PRESENT | PTE_WRITABLE;
    }

    PAGE_TABLE_ENTRY *pt = (PAGE_TABLE_ENTRY *)(pd[pdIndex] & PAGE_MASK);
    UINT64 ptIndex = PT_INDEX(alignedAddrVirt);
    if (pt[ptIndex] & PTE_PRESENT)
    {
        LOG_ERROR(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
        return EFI_INVALID_PARAMETER;
    }

    pt[ptIndex] = alignedAddrPhys | flags | isPresent;
    return EFI_SUCCESS;
}

EFI_STATUS
MapRegion(HOB_BALLOC *allocator, UINT64 pml4BasePhys, UINT64 physStart, UINT64 virtStart, UINT64 size, UINT64 flags)
{
    EFI_STATUS status;
    UINT64 offset = 0;

    if (HobAllocError(allocator))
    {
        return EFI_OUT_OF_RESOURCES;
    }

    LOG_DEBUG("Mapping %p -> %p, size=%x, flags=%x\r\n", physStart, virtStart, size, flags);

    while (offset < size)
    {
        UINT64 remaining = size - offset;
        UINT64 currPhys = physStart + offset;
        UINT64 currVirt = virtStart + offset;
        UINT64 pageSize = PAGE_4KB;
        UINT64 pageFlags = flags;

        if (remaining >= PAGE_2MB && HO_IS_ALIGNED(currPhys, PAGE_2MB) && HO_IS_ALIGNED(currVirt, PAGE_2MB))
        {
            pageSize = PAGE_2MB;
            pageFlags |= PTE_PAGE_SIZE;
        }

        MAP_PAGE_PARAMS req;
        memset(&req, 0, sizeof(MAP_PAGE_PARAMS));
        req.NotPresent = FALSE;
        req.TableBasePhys = pml4BasePhys;
        req.BumpAllocator = allocator;
        req.AddrPhys = currPhys;
        req.AddrVirt = currVirt;
        req.Flags = pageFlags;
        req.PageSize = pageSize;
        status = MapPage(&req);
        if (EFI_ERROR(status))
        {
            LOG_ERROR(L"MapRegion failed at Virt=%lx Phys=%lx Size=%lx\n", currVirt, currPhys, pageSize);
            return status;
        }

        offset += pageSize;
    }

    return EFI_SUCCESS;
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

    return g_ST->BootServices->GetMemoryMap(&map->DescriptorTotalSize, map->Segs, &map->MemoryMapKey,
                                            &map->DescriptorSize, &map->DescriptorVersion);
}

// WARNING: this function only maps first level table
static EFI_STATUS
MapAcpiTables(HOB_BALLOC *allocator, UINT64 pml4BasePhys, HO_PHYSICAL_ADDRESS rsdpPhys)
{
    if (rsdpPhys == 0)
        return EFI_INVALID_PARAMETER;

    ACPI_RSDP *rsdp = (ACPI_RSDP *)(UINTN)rsdpPhys;
    EFI_STATUS status = MapAcpiRange(allocator, pml4BasePhys, rsdpPhys, sizeof(ACPI_RSDP));
    if (EFI_ERROR(status))
        return status;

    UINT64 rootPhys = 0;
    UINT64 entrySize = 0;
    if (rsdp->Revision >= 2 && rsdp->XsdtPhys != 0)
    {
        rootPhys = rsdp->XsdtPhys;
        entrySize = sizeof(UINT64);
    }
    else if (rsdp->RsdtPhys != 0)
    {
        rootPhys = rsdp->RsdtPhys;
        entrySize = sizeof(UINT32);
    }
    else
    {
        return EFI_NOT_FOUND;
    }

    ACPI_SDT_HEADER *rootTable = (ACPI_SDT_HEADER *)(UINTN)rootPhys;
    if (rootTable->Length < sizeof(ACPI_SDT_HEADER))
        return EFI_INVALID_PARAMETER;

    status = MapAcpiRange(allocator, pml4BasePhys, rootPhys, rootTable->Length);
    if (EFI_ERROR(status))
        return status;

    UINT64 entryCount = (rootTable->Length - sizeof(ACPI_SDT_HEADER)) / entrySize;
    UINT8 *entries = (UINT8 *)rootTable + sizeof(ACPI_SDT_HEADER);
    for (UINT64 i = 0; i < entryCount; ++i)
    {
        UINT64 tablePhys = 0;
        if (entrySize == sizeof(UINT64))
        {
            tablePhys = ((UINT64 *)entries)[i];
        }
        else
        {
            tablePhys = ((UINT32 *)entries)[i];
        }

        if (tablePhys == 0)
            continue;

        ACPI_SDT_HEADER *tableHeader = (ACPI_SDT_HEADER *)(UINTN)tablePhys;
        if (tableHeader->Length < sizeof(ACPI_SDT_HEADER))
            continue;

        status = MapAcpiRange(allocator, pml4BasePhys, tablePhys, tableHeader->Length);
        if (EFI_ERROR(status))
            return status;

        // If this is HPET table, also map the HPET MMIO region
        if (tableHeader->Signature[0] == 'H' && tableHeader->Signature[1] == 'P' &&
            tableHeader->Signature[2] == 'E' && tableHeader->Signature[3] == 'T')
        {
            ACPI_HPET *hpet = (ACPI_HPET *)tableHeader;
            if (hpet->AddressSpaceId == 0 && hpet->BaseAddressPhys != 0)
            {
                // Map HPET MMIO region (typically 1KB, map 4KB to be safe)
                status = MapRegion(allocator, pml4BasePhys, hpet->BaseAddressPhys,
                                   HHDM_BASE_VA + hpet->BaseAddressPhys, PAGE_4KB,
                                   PTE_CACHE_DISABLE | PTE_WRITABLE | PTE_NO_EXECUTE);
                if (EFI_ERROR(status))
                {
                    LOG_WARNING("Failed to map HPET MMIO at %p\r\n", hpet->BaseAddressPhys);
                }
            }
        }
    }

    return EFI_SUCCESS;
}

static EFI_STATUS
MapAcpiRange(HOB_BALLOC *allocator, UINT64 pml4BasePhys, UINT64 tablePhys, UINT32 length)
{
    UINT64 startPhys = HO_ALIGN_DOWN(tablePhys, PAGE_4KB);
    UINT64 endPhys = HO_ALIGN_UP(tablePhys + length, PAGE_4KB);
    UINT64 mapSize = endPhys - startPhys;

    if (mapSize == 0)
        return EFI_INVALID_PARAMETER;

    return MapRegion(allocator, pml4BasePhys, startPhys, HHDM_BASE_VA + startPhys, mapSize, PTE_NO_EXECUTE);
}
