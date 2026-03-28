/**
 * HimuOperatingSystem
 *
 * File: blmm/acpi.c
 * Description: ACPI mapping helpers for the EFI bootloader.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "blmm_internal.h"

static EFI_STATUS MapAcpiRange(HOB_BALLOC *allocator,
                               UINT64 pml4BasePhys,
                               UINT64 tablePhys,
                               UINT32 length,
                               UINT64 *startPhysOut,
                               UINT64 *mapSizeOut);
static uint32_t AcpiSignatureToUint32(const char signature[4]);

// WARNING: this function only maps first level table
EFI_STATUS
MapAcpiTables(HOB_BALLOC *allocator, UINT64 pml4BasePhys, BOOT_CAPSULE *capsule, HO_PHYSICAL_ADDRESS rsdpPhys)
{
    if (rsdpPhys == 0)
        return EFI_INVALID_PARAMETER;

    ACPI_RSDP *rsdp = (ACPI_RSDP *)(UINTN)rsdpPhys;
    UINT64 mappedStart = 0;
    UINT64 mappedSize = 0;
    UINT64 nxFlag = GetNxFlag();
    EFI_STATUS status = MapAcpiRange(allocator, pml4BasePhys, rsdpPhys, sizeof(ACPI_RSDP), &mappedStart, &mappedSize);
    if (EFI_ERROR(status))
        return status;

    BOOT_MAPPING_RECORD_PARAMS record;
    memset(&record, 0, sizeof(record));
    record.VirtualStart = HHDM_BASE_VA + mappedStart;
    record.PhysicalStart = mappedStart;
    record.Size = mappedSize;
    record.Attributes = nxFlag;
    record.ProvenanceValue = BOOT_MAPPING_FOURCC('R', 'S', 'D', 'P');
    record.Type = BOOT_MAPPING_REGION_ACPI_RSDP;
    record.Provenance = BOOT_MAPPING_PROVENANCE_ACPI_SIGNATURE;
    record.Lifetime = BOOT_MAPPING_LIFETIME_FIRMWARE;
    record.Granularity = DetectBootMappingGranularity(mappedStart, HHDM_BASE_VA + mappedStart, mappedSize);
    status = BootMappingManifestRecord(capsule, &record);
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

    status = MapAcpiRange(allocator, pml4BasePhys, rootPhys, rootTable->Length, &mappedStart, &mappedSize);
    if (EFI_ERROR(status))
        return status;

    memset(&record, 0, sizeof(record));
    record.VirtualStart = HHDM_BASE_VA + mappedStart;
    record.PhysicalStart = mappedStart;
    record.Size = mappedSize;
    record.Attributes = nxFlag;
    record.ProvenanceValue = AcpiSignatureToUint32(rootTable->Signature);
    record.Type = BOOT_MAPPING_REGION_ACPI_ROOT;
    record.Provenance = BOOT_MAPPING_PROVENANCE_ACPI_SIGNATURE;
    record.Lifetime = BOOT_MAPPING_LIFETIME_FIRMWARE;
    record.Granularity = DetectBootMappingGranularity(mappedStart, HHDM_BASE_VA + mappedStart, mappedSize);
    status = BootMappingManifestRecord(capsule, &record);
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

        status = MapAcpiRange(allocator, pml4BasePhys, tablePhys, tableHeader->Length, &mappedStart, &mappedSize);
        if (EFI_ERROR(status))
            return status;

        memset(&record, 0, sizeof(record));
        record.VirtualStart = HHDM_BASE_VA + mappedStart;
        record.PhysicalStart = mappedStart;
        record.Size = mappedSize;
        record.Attributes = nxFlag;
        record.ProvenanceValue = AcpiSignatureToUint32(tableHeader->Signature);
        record.Type = BOOT_MAPPING_REGION_ACPI_TABLE;
        record.Provenance = BOOT_MAPPING_PROVENANCE_ACPI_SIGNATURE;
        record.Lifetime = BOOT_MAPPING_LIFETIME_FIRMWARE;
        record.Granularity = DetectBootMappingGranularity(mappedStart, HHDM_BASE_VA + mappedStart, mappedSize);
        status = BootMappingManifestRecord(capsule, &record);
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
                else
                {
                    memset(&record, 0, sizeof(record));
                    record.VirtualStart = HHDM_BASE_VA + hpet->BaseAddressPhys;
                    record.PhysicalStart = hpet->BaseAddressPhys;
                    record.Size = PAGE_4KB;
                    record.Attributes = PTE_CACHE_DISABLE | PTE_WRITABLE | nxFlag;
                    record.ProvenanceValue = BOOT_MAPPING_FOURCC('H', 'P', 'E', 'T');
                    record.Type = BOOT_MAPPING_REGION_HPET_MMIO;
                    record.Provenance = BOOT_MAPPING_PROVENANCE_ACPI_SIGNATURE;
                    record.Lifetime = BOOT_MAPPING_LIFETIME_DEVICE;
                    record.Granularity = BOOT_MAPPING_GRANULARITY_4KB;
                    status = BootMappingManifestRecord(capsule, &record);
                    if (EFI_ERROR(status))
                        return status;
                }
            }
        }
    }

    return EFI_SUCCESS;
}

static EFI_STATUS
MapAcpiRange(HOB_BALLOC *allocator,
             UINT64 pml4BasePhys,
             UINT64 tablePhys,
             UINT32 length,
             UINT64 *startPhysOut,
             UINT64 *mapSizeOut)
{
    UINT64 startPhys = HO_ALIGN_DOWN(tablePhys, PAGE_4KB);
    UINT64 endPhys = HO_ALIGN_UP(tablePhys + length, PAGE_4KB);
    UINT64 mapSize = endPhys - startPhys;

    if (mapSize == 0)
        return EFI_INVALID_PARAMETER;

    if (startPhysOut)
        *startPhysOut = startPhys;
    if (mapSizeOut)
        *mapSizeOut = mapSize;

    return MapRegion(allocator, pml4BasePhys, startPhys, HHDM_BASE_VA + startPhys, mapSize, GetNxFlag());
}

static uint32_t
AcpiSignatureToUint32(const char signature[4])
{
    return BOOT_MAPPING_FOURCC(signature[0], signature[1], signature[2], signature[3]);
}
