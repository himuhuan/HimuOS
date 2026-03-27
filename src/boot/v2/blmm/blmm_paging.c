/**
 * HimuOperatingSystem
 *
 * File: blmm_paging.c
 * Description: Page table engine — MapPage, MapRegion, identity window mapping.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "blmm_internal.h"

EFI_STATUS
BlMapInitialIdentityWindow(HOB_BALLOC *allocator, UINT64 pml4BasePhys, BOOT_CAPSULE *capsule, UINT64 size, UINT64 flags)
{
    EFI_STATUS status;
    UINT64 lowWindowSize = (size < PAGE_2MB) ? size : PAGE_2MB;

    for (UINT64 offset = 0; offset < lowWindowSize; offset += PAGE_4KB)
    {
        MAP_PAGE_PARAMS req;
        memset(&req, 0, sizeof(MAP_PAGE_PARAMS));
        req.NotPresent = BootNullPointerDetectionEnabled() && offset == 0;
        req.TableBasePhys = pml4BasePhys;
        req.BumpAllocator = allocator;
        req.AddrPhys = offset;
        req.AddrVirt = offset;
        req.Flags = flags;
        req.PageSize = PAGE_4KB;
        status = MapPage(&req);
        if (EFI_ERROR(status))
        {
            LOG_ERROR(L"MapInitialIdentityWindow failed at Virt=%lx Phys=%lx\r\n", offset, offset);
            return status;
        }
    }

    UINT64 presentStart = BootNullPointerDetectionEnabled() ? PAGE_4KB : 0;
    if (lowWindowSize > presentStart)
    {
        BOOT_MAPPING_MANIFEST_ENTRY entry;
        memset(&entry, 0, sizeof(entry));
        entry.Category = BOOT_MAPPING_CATEGORY_BOOTSTRAP_IDENTITY;
        entry.CacheType = BlGetBootMappingCacheType(flags);
        entry.Attributes = BlGetBootMappingEntryAttributes(flags,
                                                           BOOT_MAPPING_ATTR_BOOT_IMPORTED |
                                                               BOOT_MAPPING_ATTR_RELEASABLE |
                                                               BOOT_MAPPING_ATTR_BOOTSTRAP_ONLY);
        entry.VirtStart = presentStart;
        entry.VirtSize = lowWindowSize - presentStart;
        entry.PhysStart = presentStart;
        entry.PhysSize = lowWindowSize - presentStart;
        entry.PageSize = PAGE_4KB;
        entry.RawPteFlags = flags;
        status = BlAppendBootMappingEntry(capsule, &entry);
        if (EFI_ERROR(status))
            return status;
    }

    if (size <= lowWindowSize)
        return EFI_SUCCESS;

    return MapRegion(allocator, pml4BasePhys, lowWindowSize, lowWindowSize, size - lowWindowSize, flags, capsule,
                     BOOT_MAPPING_CATEGORY_BOOTSTRAP_IDENTITY,
                     BOOT_MAPPING_ATTR_BOOT_IMPORTED | BOOT_MAPPING_ATTR_RELEASABLE |
                         BOOT_MAPPING_ATTR_BOOTSTRAP_ONLY);
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
    UINT64 flagBits = params->Flags & ~PTE_PRESENT;
    UINT64 flags = (params->PageSize != PAGE_4KB) ? (flagBits | PTE_PAGE_SIZE) : flagBits;
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
            if (!(pdpt[pdptIndex] & PTE_PAGE_SIZE))
            {
                LOG_ERROR(L"Virtual address already mapped by non-1GB entry: 0x%x\r\n", alignedAddrVirt);
                return EFI_INVALID_PARAMETER;
            }
            if (BlIsSameMapping(pdpt[pdptIndex], alignedAddrPhys, flags, isPresent))
                return EFI_SUCCESS;
            LOG_ERROR(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
        pdpt[pdptIndex] = alignedAddrPhys | flags | isPresent;
        return EFI_SUCCESS;
    }

    if (pdpt[pdptIndex] & PTE_PRESENT)
    {
        if (pdpt[pdptIndex] & PTE_PAGE_SIZE)
        {
            LOG_ERROR(L"Virtual address already mapped by 1GB entry: 0x%x\r\n", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
    }
    else
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
            if (!(pd[pdIndex] & PTE_PAGE_SIZE))
            {
                LOG_ERROR(L"Virtual address already mapped by non-2MB entry: 0x%x\r\n", alignedAddrVirt);
                return EFI_INVALID_PARAMETER;
            }
            if (BlIsSameMapping(pd[pdIndex], alignedAddrPhys, flags, isPresent))
                return EFI_SUCCESS;
            LOG_ERROR(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
        pd[pdIndex] = alignedAddrPhys | flags | isPresent;
        return EFI_SUCCESS;
    }

    if (pd[pdIndex] & PTE_PRESENT)
    {
        if (pd[pdIndex] & PTE_PAGE_SIZE)
        {
            LOG_ERROR(L"Virtual address already mapped by 2MB entry: 0x%x\r\n", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
    }
    else
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
        if (BlIsSameMapping(pt[ptIndex], alignedAddrPhys, flags, isPresent))
            return EFI_SUCCESS;
        LOG_ERROR(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
        return EFI_INVALID_PARAMETER;
    }

    pt[ptIndex] = alignedAddrPhys | flags | isPresent;
    return EFI_SUCCESS;
}

EFI_STATUS
MapRegion(HOB_BALLOC *allocator,
          UINT64 pml4BasePhys,
          UINT64 physStart,
          UINT64 virtStart,
          UINT64 size,
          UINT64 flags,
          BOOT_CAPSULE *capsule,
          UINT32 category,
          UINT64 manifestAttributes)
{
    EFI_STATUS status;
    UINT64 offset = 0;
    BOOT_MAPPING_MANIFEST_ENTRY pendingEntry;
    BOOL hasPendingEntry = FALSE;

    if (HobAllocError(allocator))
    {
        return EFI_OUT_OF_RESOURCES;
    }

    // LOG_DEBUG("Mapping %p -> %p, size=%x, flags=%x\r\n", physStart, virtStart, size, flags);

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

        if (capsule != NULL && category != BOOT_MAPPING_CATEGORY_NONE)
        {
            BOOT_MAPPING_MANIFEST_ENTRY currentEntry;
            memset(&currentEntry, 0, sizeof(currentEntry));
            currentEntry.Category = category;
            currentEntry.CacheType = BlGetBootMappingCacheType(pageFlags);
            currentEntry.Attributes = BlGetBootMappingEntryAttributes(pageFlags, manifestAttributes);
            currentEntry.VirtStart = currVirt;
            currentEntry.VirtSize = pageSize;
            currentEntry.PhysStart = currPhys;
            currentEntry.PhysSize = pageSize;
            currentEntry.PageSize = pageSize;
            currentEntry.RawPteFlags = pageFlags;

            if (hasPendingEntry && pendingEntry.Category == currentEntry.Category &&
                pendingEntry.CacheType == currentEntry.CacheType && pendingEntry.Attributes == currentEntry.Attributes &&
                pendingEntry.PageSize == currentEntry.PageSize && pendingEntry.RawPteFlags == currentEntry.RawPteFlags &&
                pendingEntry.VirtStart + pendingEntry.VirtSize == currentEntry.VirtStart &&
                pendingEntry.PhysStart + pendingEntry.PhysSize == currentEntry.PhysStart)
            {
                pendingEntry.VirtSize += currentEntry.VirtSize;
                pendingEntry.PhysSize += currentEntry.PhysSize;
            }
            else
            {
                if (hasPendingEntry)
                {
                    status = BlAppendBootMappingEntry(capsule, &pendingEntry);
                    if (EFI_ERROR(status))
                        return status;
                }
                BlCopyBootMappingManifestEntry(&pendingEntry, &currentEntry);
                hasPendingEntry = TRUE;
            }
        }

        offset += pageSize;
    }

    if (hasPendingEntry)
    {
        status = BlAppendBootMappingEntry(capsule, &pendingEntry);
        if (EFI_ERROR(status))
            return status;
    }

    return EFI_SUCCESS;
}

BOOL
BlIsSameMapping(UINT64 existingEntry, UINT64 targetPhys, UINT64 flags, UINT64 isPresent)
{
    UINT64 mask = PAGE_MASK | PTE_PRESENT | PTE_WRITABLE | PTE_USER | PTE_WRITETHROUGH | PTE_CACHE_DISABLE |
                  PTE_GLOBAL | PTE_PAGE_SIZE | PTE_NO_EXECUTE;
    UINT64 expected = (targetPhys & PAGE_MASK) | flags | isPresent;
    return ((existingEntry & mask) == (expected & mask)) ? TRUE : FALSE;
}
