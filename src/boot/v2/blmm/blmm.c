#include "blmm_internal.h"

#define MIN_MEMMAP_PAGES                 3 // 12KB for memory map at least

#define BOOT_MAPPING_FIXED_ENTRY_BUDGET  10U
#define BOOT_MAPPING_EXTRA_SLACK_ENTRIES 8U

static EFI_MEMORY_MAP *InitMemoryMap(void *base, size_t size);
static EFI_STATUS FillMemoryMap(EFI_MEMORY_MAP *map);
static UINT64 GetMemoryDescriptorCount(const EFI_MEMORY_MAP *memoryMap);
static uint32_t CountAcpiManifestEntries(HO_PHYSICAL_ADDRESS rsdpPhys);
static uint32_t AcpiSignatureToUint32(const char signature[4]);

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
        status = BootValidateGuardedAllocation(L"loader memory map buffer", mapBuffer, mapBufferPages);
        if (EFI_ERROR(status))
        {
            (void)g_ST->BootServices->FreePages(mapBuffer, mapBufferPages);
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
EstimateBootMappingManifestSize(const EFI_MEMORY_MAP *memoryMap, HO_PHYSICAL_ADDRESS rsdpPhys)
{
    UINT64 descCount = GetMemoryDescriptorCount(memoryMap);
    UINT64 entryCapacity = descCount + CountAcpiManifestEntries(rsdpPhys) + BOOT_MAPPING_FIXED_ENTRY_BUDGET +
                           BOOT_MAPPING_EXTRA_SLACK_ENTRIES;
    return BootMappingManifestTotalSizeForCapacity((uint32_t)entryCapacity);
}

EFI_STATUS
BootMappingManifestInitialize(BOOT_CAPSULE *capsule)
{
    if (!capsule || capsule->ManifestPhys == 0 || capsule->Layout.ManifestSize < BootMappingManifestHeaderSize())
        return EFI_INVALID_PARAMETER;

    BOOT_MAPPING_MANIFEST_HEADER *manifest = BootGetMappingManifest(capsule);
    if (!manifest)
        return EFI_INVALID_PARAMETER;

    memset(manifest, 0, capsule->Layout.ManifestSize);

    uint32_t totalSize = (uint32_t)capsule->Layout.ManifestSize;
    uint32_t headerSize = BootMappingManifestHeaderSize();
    uint32_t entrySize = (uint32_t)sizeof(BOOT_MAPPING_MANIFEST_ENTRY);
    if (totalSize < headerSize + entrySize)
        return EFI_BUFFER_TOO_SMALL;

    manifest->Magic = BOOT_MAPPING_MANIFEST_MAGIC;
    manifest->Version = BOOT_MAPPING_MANIFEST_VERSION;
    manifest->HeaderSize = (uint16_t)headerSize;
    manifest->TotalSize = totalSize;
    manifest->EntrySize = entrySize;
    manifest->EntryCount = 0;
    manifest->EntryCapacity = (totalSize - headerSize) / entrySize;
    return (manifest->EntryCapacity == 0) ? EFI_BUFFER_TOO_SMALL : EFI_SUCCESS;
}

EFI_STATUS
BootMappingManifestRecord(BOOT_CAPSULE *capsule, const BOOT_MAPPING_RECORD_PARAMS *params)
{
    if (!capsule || !params)
        return EFI_INVALID_PARAMETER;

    if (params->Size == 0)
        return EFI_SUCCESS;

    if (!HO_IS_ALIGNED(params->VirtualStart, PAGE_4KB) || !HO_IS_ALIGNED(params->PhysicalStart, PAGE_4KB) ||
        !HO_IS_ALIGNED(params->Size, PAGE_4KB))
        return EFI_INVALID_PARAMETER;

    if (params->Type <= BOOT_MAPPING_REGION_INVALID || params->Type >= BOOT_MAPPING_REGION_MAX ||
        params->Provenance >= BOOT_MAPPING_PROVENANCE_MAX || params->Lifetime <= BOOT_MAPPING_LIFETIME_INVALID ||
        params->Lifetime >= BOOT_MAPPING_LIFETIME_MAX || params->Granularity <= BOOT_MAPPING_GRANULARITY_INVALID ||
        params->Granularity >= BOOT_MAPPING_GRANULARITY_MAX)
        return EFI_INVALID_PARAMETER;

    BOOT_MAPPING_MANIFEST_HEADER *manifest = BootGetMappingManifest(capsule);
    if (!manifest || manifest->Magic != BOOT_MAPPING_MANIFEST_MAGIC ||
        manifest->Version != BOOT_MAPPING_MANIFEST_VERSION ||
        manifest->EntrySize != sizeof(BOOT_MAPPING_MANIFEST_ENTRY) ||
        manifest->HeaderSize != BootMappingManifestHeaderSize())
        return EFI_INVALID_PARAMETER;

    if (manifest->EntryCount >= manifest->EntryCapacity)
        return EFI_OUT_OF_RESOURCES;

    BOOT_MAPPING_MANIFEST_ENTRY *entries = BootMappingManifestEntries(manifest);
    BOOT_MAPPING_MANIFEST_ENTRY *entry = &entries[manifest->EntryCount++];
    memset(entry, 0, sizeof(*entry));
    entry->VirtualStart = params->VirtualStart;
    entry->PhysicalStart = params->PhysicalStart;
    entry->Size = params->Size;
    entry->Attributes = params->Attributes | PTE_PRESENT;
    entry->ProvenanceValue = params->ProvenanceValue;
    entry->Type = (uint16_t)params->Type;
    entry->Provenance = (uint8_t)params->Provenance;
    entry->Lifetime = (uint8_t)params->Lifetime;
    entry->Granularity = (uint8_t)params->Granularity;
    return EFI_SUCCESS;
}

BOOT_MAPPING_GRANULARITY
DetectBootMappingGranularity(HO_PHYSICAL_ADDRESS physStart, HO_VIRTUAL_ADDRESS virtStart, uint64_t size)
{
    uint64_t offset = 0;
    BOOT_MAPPING_GRANULARITY detected = BOOT_MAPPING_GRANULARITY_INVALID;

    if (size == 0)
        return BOOT_MAPPING_GRANULARITY_INVALID;

    while (offset < size)
    {
        uint64_t remaining = size - offset;
        uint64_t currPhys = physStart + offset;
        uint64_t currVirt = virtStart + offset;
        BOOT_MAPPING_GRANULARITY current = BOOT_MAPPING_GRANULARITY_4KB;
        uint64_t pageSize = PAGE_4KB;

        if (remaining >= PAGE_2MB && HO_IS_ALIGNED(currPhys, PAGE_2MB) && HO_IS_ALIGNED(currVirt, PAGE_2MB))
        {
            current = BOOT_MAPPING_GRANULARITY_2MB;
            pageSize = PAGE_2MB;
        }

        if (detected == BOOT_MAPPING_GRANULARITY_INVALID)
        {
            detected = current;
        }
        else if (detected != current)
        {
            return BOOT_MAPPING_GRANULARITY_MIXED;
        }

        offset += pageSize;
    }

    return detected;
}

void
DumpBootMappingManifestSummary(const BOOT_CAPSULE *capsule)
{
    const BOOT_MAPPING_MANIFEST_HEADER *manifest = BootGetConstMappingManifest(capsule);
    if (!manifest || manifest->Magic != BOOT_MAPPING_MANIFEST_MAGIC)
    {
        LOG_WARNING("Boot Mapping Manifest unavailable or invalid\r\n");
        return;
    }

    uint32_t identityCount = 0;
    uint32_t hhdmCount = 0;
    uint32_t bootCount = 0;
    uint32_t kernelCount = 0;
    uint32_t acpiCount = 0;
    uint32_t mmioCount = 0;
    const BOOT_MAPPING_MANIFEST_ENTRY *entries = BootMappingManifestConstEntries(manifest);

    for (uint32_t idx = 0; idx < manifest->EntryCount; ++idx)
    {
        const BOOT_MAPPING_MANIFEST_ENTRY *entry = &entries[idx];
        switch ((BOOT_MAPPING_REGION_TYPE)entry->Type)
        {
        case BOOT_MAPPING_REGION_IDENTITY:
            identityCount++;
            break;
        case BOOT_MAPPING_REGION_HHDM:
            hhdmCount++;
            break;
        case BOOT_MAPPING_REGION_BOOT_STAGING:
        case BOOT_MAPPING_REGION_BOOT_HANDOFF:
        case BOOT_MAPPING_REGION_BOOT_PAGE_TABLES:
            bootCount++;
            break;
        case BOOT_MAPPING_REGION_KERNEL_CODE:
        case BOOT_MAPPING_REGION_KERNEL_DATA:
        case BOOT_MAPPING_REGION_KERNEL_STACK:
        case BOOT_MAPPING_REGION_KERNEL_IST_STACK:
            kernelCount++;
            break;
        case BOOT_MAPPING_REGION_ACPI_RSDP:
        case BOOT_MAPPING_REGION_ACPI_ROOT:
        case BOOT_MAPPING_REGION_ACPI_TABLE:
            acpiCount++;
            break;
        case BOOT_MAPPING_REGION_FRAMEBUFFER:
        case BOOT_MAPPING_REGION_HPET_MMIO:
        case BOOT_MAPPING_REGION_LAPIC_MMIO:
            mmioCount++;
            break;
        default:
            break;
        }
    }

    LOG_INFO("Boot Mapping Manifest: phys=%p size=%u used=%u entries=%u/%u (identity=%u hhdm=%u boot=%u kernel=%u "
             "acpi=%u mmio=%u)\r\n",
             (void *)(UINTN)capsule->ManifestPhys, (UINT64)manifest->TotalSize,
             (UINT64)BootMappingManifestUsedSize(manifest), (UINT64)manifest->EntryCount,
             (UINT64)manifest->EntryCapacity, (UINT64)identityCount, (UINT64)hhdmCount, (UINT64)bootCount,
             (UINT64)kernelCount, (UINT64)acpiCount, (UINT64)mmioCount);
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

static UINT64
GetMemoryDescriptorCount(const EFI_MEMORY_MAP *memoryMap)
{
    if (!memoryMap)
        return 0;

    if (memoryMap->DescriptorCount != 0)
        return memoryMap->DescriptorCount;

    if (memoryMap->DescriptorSize == 0)
        return 0;

    return memoryMap->DescriptorTotalSize / memoryMap->DescriptorSize;
}

static uint32_t
CountAcpiManifestEntries(HO_PHYSICAL_ADDRESS rsdpPhys)
{
    if (rsdpPhys == 0)
        return 0;

    uint32_t count = 1; // RSDP
    ACPI_RSDP *rsdp = (ACPI_RSDP *)(UINTN)rsdpPhys;
    uint64_t rootPhys = 0;
    uint64_t entrySize = 0;

    if (rsdp->Revision >= 2 && rsdp->XsdtPhys != 0)
    {
        rootPhys = rsdp->XsdtPhys;
        entrySize = sizeof(uint64_t);
    }
    else if (rsdp->RsdtPhys != 0)
    {
        rootPhys = rsdp->RsdtPhys;
        entrySize = sizeof(uint32_t);
    }
    else
    {
        return count + 4;
    }

    ACPI_SDT_HEADER *rootTable = (ACPI_SDT_HEADER *)(UINTN)rootPhys;
    if (rootTable->Length < sizeof(ACPI_SDT_HEADER))
        return count + 4;

    count++;
    uint64_t entryCount = (rootTable->Length - sizeof(ACPI_SDT_HEADER)) / entrySize;
    uint8_t *entries = (uint8_t *)rootTable + sizeof(ACPI_SDT_HEADER);

    for (uint64_t idx = 0; idx < entryCount; ++idx)
    {
        uint64_t tablePhys = (entrySize == sizeof(uint64_t)) ? ((uint64_t *)entries)[idx] : ((uint32_t *)entries)[idx];
        if (tablePhys == 0)
            continue;

        count++;

        ACPI_SDT_HEADER *tableHeader = (ACPI_SDT_HEADER *)(UINTN)tablePhys;
        if (tableHeader->Length < sizeof(ACPI_SDT_HEADER))
            continue;

        if (AcpiSignatureToUint32(tableHeader->Signature) == BOOT_MAPPING_FOURCC('H', 'P', 'E', 'T'))
        {
            ACPI_HPET *hpet = (ACPI_HPET *)tableHeader;
            if (hpet->AddressSpaceId == 0 && hpet->BaseAddressPhys != 0)
                count++;
        }
    }

    return count;
}

static uint32_t
AcpiSignatureToUint32(const char signature[4])
{
    return BOOT_MAPPING_FOURCC(signature[0], signature[1], signature[2], signature[3]);
}
