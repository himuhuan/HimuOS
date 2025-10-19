#include "efi.h"
#include "io.h"
#include "alloc.h"
#include "../boot.h"
#include "arch/amd64/pm.h"

// ===========================================
// Global varibles

struct EFI_SYSTEM_TABLE *g_ST;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL *g_GOP;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *g_FSP;

EFI_HANDLE gImageHandle;

// ==========================================

HO_NODISCARD UINT64
GetStatusCodeLow(UINT64 code)
{
    return code & 0x1FFFFFFFFFFFFFFFULL;
}

void
EfiInitialize(EFI_HANDLE imageHandle, struct EFI_SYSTEM_TABLE *SystemTable)
{
    static struct EFI_GUID gop_guid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
    static struct EFI_GUID sfsp_guid = {0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
    EFI_STATUS status;

    g_ST = SystemTable;
    g_ST->BootServices->SetWatchdogTimer(0, 0, 0, NULL);
    ConsoleWriteStr(TEXT("HimuOS UEFI Boot Manager Initializing...\r\n"));

    if ((status = g_ST->BootServices->LocateProtocol(&gop_guid, NULL, (void **)&g_GOP)) != EFI_SUCCESS)
    {
        PRINT_HEX_WITH_MESSAGE("Failed to locate Graphics Output Protocol: ", status);
        return;
    }

    if ((status = g_ST->BootServices->LocateProtocol(&sfsp_guid, NULL, (void **)&g_FSP)) != EFI_SUCCESS)
    {
        PRINT_HEX_WITH_MESSAGE("Failed to locate Simple File System Protocol: ", status);
        return;
    }

    gImageHandle = imageHandle;

    ConsoleWriteStr(TEXT("\r\n**Welcome to HimuOS UEFI Boot Manager Shell!**\r\n"));
}


HO_NODISCARD HO_KERNEL_API int
ListDir(IN CHAR16 *dir, OUT FILE_INFO *files, IN uint64_t maxFiles, OUT uint64_t *fileCount)
{
    EFI_FILE_PROTOCOL *rootDir = NULL;
    EFI_FILE_PROTOCOL *targetDir = NULL;
    EFI_STATUS status;
    UINT8 buffer[MAX_FILEINFO_SIZ];
    EFI_FILE_INFO *fileInfo;
    UINT64 count = 0;
    BOOL overflow = FALSE;

    *fileCount = 0;

    status = g_FSP->OpenVolume(g_FSP, &rootDir);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to open root volume: ", status);
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (dir && *dir != L'\0')
    {
        status = rootDir->Open(rootDir, &targetDir, dir, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status))
        {
            PRINT_HEX_WITH_MESSAGE("Failed to open directory: ", status);
            rootDir->Close(rootDir);
            return EC_NOT_ENOUGH_MEMORY;
        }
    }
    else
    {
        targetDir = rootDir;
    }

    overflow = TRUE;
    while (count < maxFiles)
    {
        UINT64 readSize = sizeof(buffer);
        status = targetDir->Read(targetDir, &readSize, buffer);

        if (EFI_ERROR(status))
        {
            PRINT_HEX_WITH_MESSAGE("Failed to read directory: ", status);
            break;
        }

        if (readSize == 0)
        {
            overflow = FALSE;
            break;
        }

        fileInfo = (EFI_FILE_INFO *)buffer;

        files[count].Size = fileInfo->Size;
        files[count].FileSize = fileInfo->FileSize;
        files[count].PhysicalSize = fileInfo->PhysicalSize;
        files[count].CreateTime = fileInfo->CreateTime;
        files[count].LastAccessTime = fileInfo->LastAccessTime;
        files[count].ModificationTime = fileInfo->ModificationTime;
        files[count].Attribute = fileInfo->Attribute;
        memset(files[count].FileName, 0, sizeof(files[count].FileName));
        CopyString(files[count].FileName, fileInfo->FileName);

        count++;
    }

    *fileCount = count;

    if (overflow)
        return EC_NOT_ENOUGH_MEMORY;

    g_ST->BootServices->FreePool(buffer);
    if (targetDir != rootDir)
        targetDir->Close(targetDir);
    rootDir->Close(rootDir);

    return EC_SUCCESS;
}

EFI_STATUS
FillMemoryMap(IN_OUT MM_INITIAL_MAP *map)
{
    uint64_t expectedMapSize = 0;

    EFI_STATUS status = g_ST->BootServices->GetMemoryMap(&expectedMapSize, NULL, &map->MemoryMapKey,
                                                         &map->DescriptorSize, &map->DescriptorVersion);
    if (GetStatusCodeLow(status) != EFI_BUFFER_TOO_SMALL)
    {
        PRINT_HEX_WITH_MESSAGE("Failed to get memory_map_size: ", status);
        return status;
    }
    expectedMapSize = HO_ALIGN_UP(expectedMapSize + 32 * map->DescriptorSize, PAGE_4KB);
    if (expectedMapSize > map->DescriptorTotalSize)
    {
        PRINT_SIZ_WITH_MESSAGE("Memory map buffer too small, required size: ", expectedMapSize);
        return EFI_BUFFER_TOO_SMALL;
    }
    return g_ST->BootServices->GetMemoryMap(&map->DescriptorTotalSize, map->Segs, &map->MemoryMapKey,
                                            &map->DescriptorSize, &map->DescriptorVersion);
}

MM_INITIAL_MAP *
GetLoaderRuntimeMemoryMap()
{
    EFI_PHYSICAL_ADDRESS mapBuffer = 0;
    EFI_STATUS status = !EFI_SUCCESS;
    MM_INITIAL_MAP *map = NULL;
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
            PRINT_HEX_WITH_MESSAGE("Failed to allocate pages for memory map: ", status);
            return NULL;
        }
        map = InitMemoryMap((void *)mapBuffer, mapBufferSize);
        status = FillMemoryMap(map);
        mapBufferPages++;
    }

    return map;
}
