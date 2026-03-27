/**
 * HimuOperatingSystem
 *
 * File: blmm_hhdm_acpi.c
 * Description: HHDM and ACPI table mapping for the bootloader.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "blmm_internal.h"

static EFI_STATUS
BlMapAcpiRange(HOB_BALLOC *allocator, UINT64 pml4BasePhys, BOOT_CAPSULE *capsule, UINT64 tablePhys, UINT32 length);

UINT64
BlGetNxFlag(void)
{
    return BootUseNx() ? PTE_NO_EXECUTE : 0;
}

UINT64
BlGetHhdmMapFlags(EFI_MEMORY_TYPE type, UINT64 nxFlag)
{
    if (type == EfiMemoryMappedIO || type == EfiMemoryMappedIOPortSpace)
        return PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag;

    if (type == EfiACPIReclaimMemory || type == EfiACPIMemoryNVS)
        return nxFlag;

    return PTE_WRITABLE | nxFlag;
}

EFI_STATUS
BlMapFullHhdmFromMemoryMap(HOB_BALLOC *allocator,
                           UINT64 pml4BasePhys,
                           BOOT_CAPSULE *capsule,
                           EFI_MEMORY_MAP *memoryMap,
                           UINT64 nxFlag,
                           UINT64 *mappedDescCount,
                           UINT64 *highestPhysExclusive)
{
    if (!allocator || !memoryMap || !mappedDescCount || !highestPhysExclusive)
        return EFI_INVALID_PARAMETER;

    if (memoryMap->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) || memoryMap->DescriptorSize == 0)
        return EFI_INVALID_PARAMETER;

    *mappedDescCount = 0;
    *highestPhysExclusive = 0;

    UINT64 descCount = memoryMap->DescriptorTotalSize / memoryMap->DescriptorSize;
    for (UINT64 idx = 0; idx < descCount; ++idx)
    {
        UINT8 *descAddr = (UINT8 *)memoryMap->Segs + idx * memoryMap->DescriptorSize;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)descAddr;

        if (!desc || desc->NumberOfPages == 0)
            continue;

        UINT64 mapSize = desc->NumberOfPages << PAGE_SHIFT;
        UINT64 mapPhysStart = desc->PhysicalStart;
        UINT64 mapPhysEndExclusive = mapPhysStart + mapSize;
        if (mapPhysEndExclusive < mapPhysStart)
            return EFI_INVALID_PARAMETER;

        UINT64 flags = BlGetHhdmMapFlags((EFI_MEMORY_TYPE)desc->Type, nxFlag);
        EFI_STATUS status = MapRegion(allocator, pml4BasePhys, mapPhysStart, HHDM_BASE_VA + mapPhysStart, mapSize,
                                      flags, capsule, BOOT_MAPPING_CATEGORY_HHDM, BOOT_MAPPING_ATTR_BOOT_IMPORTED);
        if (EFI_ERROR(status))
        {
            LOG_ERROR("Map FULL HHDM failed at descriptor=%u type=%u phys=%p pages=%u: %k\r\n", idx, desc->Type,
                      (void *)(UINTN)mapPhysStart, desc->NumberOfPages, status);
            return status;
        }

        if (mapPhysEndExclusive > *highestPhysExclusive)
            *highestPhysExclusive = mapPhysEndExclusive;
        (*mappedDescCount)++;
    }

    return EFI_SUCCESS;
}

// WARNING: this function only maps first level table
EFI_STATUS
BlMapAcpiTables(HOB_BALLOC *allocator, UINT64 pml4BasePhys, BOOT_CAPSULE *capsule, HO_PHYSICAL_ADDRESS rsdpPhys)
{
    if (rsdpPhys == 0)
        return EFI_INVALID_PARAMETER;

    ACPI_RSDP *rsdp = (ACPI_RSDP *)(UINTN)rsdpPhys;
    EFI_STATUS status = BlMapAcpiRange(allocator, pml4BasePhys, capsule, rsdpPhys, sizeof(ACPI_RSDP));
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

    status = BlMapAcpiRange(allocator, pml4BasePhys, capsule, rootPhys, rootTable->Length);
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

        status = BlMapAcpiRange(allocator, pml4BasePhys, capsule, tablePhys, tableHeader->Length);
        if (EFI_ERROR(status))
            return status;

        // If this is HPET table, also map the HPET MMIO region
        if (tableHeader->Signature[0] == 'H' && tableHeader->Signature[1] == 'P' && tableHeader->Signature[2] == 'E' &&
            tableHeader->Signature[3] == 'T')
        {
            ACPI_HPET *hpet = (ACPI_HPET *)tableHeader;
            if (hpet->AddressSpaceId == 0 && hpet->BaseAddressPhys != 0)
            {
                status = MapRegion(allocator, pml4BasePhys, hpet->BaseAddressPhys, HHDM_BASE_VA + hpet->BaseAddressPhys,
                                   PAGE_4KB, PTE_CACHE_DISABLE | PTE_WRITABLE | BlGetNxFlag(), capsule,
                                   BOOT_MAPPING_CATEGORY_HPET_MMIO, BOOT_MAPPING_ATTR_BOOT_IMPORTED);
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
BlMapAcpiRange(HOB_BALLOC *allocator, UINT64 pml4BasePhys, BOOT_CAPSULE *capsule, UINT64 tablePhys, UINT32 length)
{
    UINT64 startPhys = HO_ALIGN_DOWN(tablePhys, PAGE_4KB);
    UINT64 endPhys = HO_ALIGN_UP(tablePhys + length, PAGE_4KB);
    UINT64 mapSize = endPhys - startPhys;

    if (mapSize == 0)
        return EFI_INVALID_PARAMETER;

    return MapRegion(allocator, pml4BasePhys, startPhys, HHDM_BASE_VA + startPhys, mapSize, BlGetNxFlag(), capsule,
                     BOOT_MAPPING_CATEGORY_ACPI, BOOT_MAPPING_ATTR_BOOT_IMPORTED);
}
