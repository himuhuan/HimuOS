#include "blmm_internal.h"

#define MIN_MEMMAP_PAGES 3 // 12KB for memory map at least
static EFI_MEMORY_MAP *InitMemoryMap(void *base, size_t size);
static EFI_STATUS FillMemoryMap(EFI_MEMORY_MAP *map);
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
