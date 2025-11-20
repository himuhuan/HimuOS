#include "efi.h"
#include "io.h"
#include "alloc.h"
#include "../boot.h"
#include "arch/amd64/pm.h"

// ===========================================
// Global variables

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
    ConsoleFormatWrite(L"HimuOS UEFI Boot Manager Initializing...\r\n");

    if ((status = g_ST->BootServices->LocateProtocol(&gop_guid, NULL, (void **)&g_GOP)) != EFI_SUCCESS)
    {
        ConsoleFormatWrite(L"Failed to locate Graphics Output Protocol: %k (0x%x)\r\n", status, status);
        return;
    }

    if ((status = g_ST->BootServices->LocateProtocol(&sfsp_guid, NULL, (void **)&g_FSP)) != EFI_SUCCESS)
    {
        ConsoleFormatWrite(L"Failed to locate Simple File System Protocol: %k (0x%x)\r\n", status, status);
        return;
    }

    gImageHandle = imageHandle;

    ConsoleFormatWrite(L"\r\n**Welcome to HimuOS UEFI Boot Manager Shell!**\r\n");
}

int
CopyString(CHAR16 *dest, const CHAR16 *src)
{
    int len = 0;

    while (src[len])
    {
        dest[len] = src[len];
        len++;
    }
    dest[len] = 0;
    return len;
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
        ConsoleFormatWrite(L"Failed to open root volume: %k (0x%x)\r\n", status, status);
        return EC_NOT_ENOUGH_MEMORY;
    }

    if (dir && *dir != L'\0')
    {
        status = rootDir->Open(rootDir, &targetDir, dir, EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR(status))
        {
            ConsoleFormatWrite(L"Failed to open directory: %k (0x%x)\r\n", status, status);
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
            ConsoleFormatWrite(L"Failed to read directory: %k (0x%x)\r\n", status, status);
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
        ConsoleFormatWrite(L"Failed to get memory_map_size: %k (0x%x)\r\n", status, status);
        return status;
    }
    expectedMapSize = HO_ALIGN_UP(expectedMapSize + 32 * map->DescriptorSize, PAGE_4KB);
    if (expectedMapSize > map->DescriptorTotalSize)
    {
        ConsoleFormatWrite(L"Memory map buffer too small, required size: %u byte\r\n", expectedMapSize);
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
            ConsoleFormatWrite(L"Failed to allocate pages for memory map: %k (0x%x)\r\n", status, status);
            return NULL;
        }
        map = InitMemoryMap((void *)mapBuffer, mapBufferSize);
        status = FillMemoryMap(map);
        mapBufferPages++;
    }

    return map;
}

