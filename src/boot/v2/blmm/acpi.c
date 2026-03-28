/**
 * HimuOperatingSystem
 *
 * File: blmm/acpi.c
 * Description: ACPI mapping helpers for the EFI bootloader.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "blmm_internal.h"

static EFI_STATUS MapAcpiRange(HOB_BALLOC *allocator, UINT64 pml4BasePhys, UINT64 tablePhys, UINT32 length);

// WARNING: this function only maps first level table
EFI_STATUS
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

        if (tableHeader->Signature[0] == 'H' && tableHeader->Signature[1] == 'P' && tableHeader->Signature[2] == 'E' &&
            tableHeader->Signature[3] == 'T')
        {
            ACPI_HPET *hpet = (ACPI_HPET *)tableHeader;
            if (hpet->AddressSpaceId == 0 && hpet->BaseAddressPhys != 0)
            {
                status = MapRegion(allocator, pml4BasePhys, hpet->BaseAddressPhys, HHDM_BASE_VA + hpet->BaseAddressPhys,
                                   PAGE_4KB, PTE_CACHE_DISABLE | PTE_WRITABLE | GetNxFlag());
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

    return MapRegion(allocator, pml4BasePhys, startPhys, HHDM_BASE_VA + startPhys, mapSize, GetNxFlag());
}
