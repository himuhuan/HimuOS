/**
 * HimuOperatingSystem
 *
 * File: shell.c
 * Description: Implementation of UEFI shell interface
 * Module: boot
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "shell.h"
#include "io.h"
#include "bootloader.h"
#include "libc/wchar.h"
#include "libc/string.h"

static SHELL_COMMAND g_command_table[] = {
    {TEXT("mem"), ShowMemoryMap}, {TEXT("boot"), Boot}, {TEXT("dir"), Dir}, {NULL, NULL} // Terminator
};

void
Shell(IN const CHAR16 *Prompt)
{
    CHAR16 *inputLineBuf = NULL;
    EFI_STATUS st =
        g_ST->BootServices->AllocatePool(EfiBootServicesData, sizeof(CHAR16) * MAX_LINE, (void **)&inputLineBuf);
    if (EFI_ERROR(st))
    {
        ConsoleFormatWrite(L"Allocate input buffer failed: %k (0x%x)\r\n", st, st);
        return;
    }

    UINT8 *parseBuf = NULL;
    st = g_ST->BootServices->AllocatePool(EfiBootServicesData, MAX_ARGBUF_SIZ, (void **)&parseBuf);
    if (EFI_ERROR(st))
    {
        ConsoleFormatWrite(L"Allocate parse buffer failed: %k (0x%x)\r\n", st, st);
        return;
    }

    while (TRUE)
    {
        ConsoleFormatWrite(L"%s", Prompt);
        if (ConsoleReadline(inputLineBuf, MAX_LINE) <= 0)
            continue;

        BOOL found = FALSE;
        for (int i = 0; g_command_table[i].CommandName != NULL; i++)
        {
            int argc = ParseCommandLine(inputLineBuf, parseBuf, MAX_ARGBUF_SIZ);
            if (argc < 0)
            {
                ConsoleFormatWrite(L"Error: command line too long\r\n");
                found = TRUE; // Prevent "command not found" message
                break;
            }

            COMMAND_STRING *command = FindCommandString(argc, parseBuf, MAX_ARGBUF_SIZ, 0);
            if (!wstrcmp(command->String, g_command_table[i].CommandName))
            {
                g_command_table[i].Function(argc, parseBuf);
                found = TRUE;
                break;
            }
        }

        if (!found)
        {
            ConsoleFormatWrite(L"Error: '%s': command not found\r\n", inputLineBuf);
        }
    }

    // Unreachable with current loop, but kept for completeness
    g_ST->BootServices->FreePool(inputLineBuf);
    g_ST->BootServices->FreePool(parseBuf);
}

CHAR16 *
MemoryTypeString(UINT32 Type)
{
    if (Type == EfiReservedMemoryType)
        return L"Reserved    ";
    if (Type == EfiLoaderCode)
        return L"LoaderCode  ";
    if (Type == EfiLoaderData)
        return L"LoaderData  ";
    if (Type == EfiBootServicesCode)
        return L"BootSvcCode ";
    if (Type == EfiBootServicesData)
        return L"BootSvcData ";
    if (Type == EfiRuntimeServicesCode)
        return L"RunSvcCode  ";
    if (Type == EfiRuntimeServicesData)
        return L"RunSvcData  ";
    if (Type == EfiConventionalMemory)
        return L"ConvMemory  ";
    if (Type == EfiUnusableMemory)
        return L"Unusable    ";
    if (Type == EfiACPIReclaimMemory)
        return L"APIC Recl   ";
    if (Type == EfiACPIMemoryNVS)
        return L"APIC MemNVS ";
    if (Type == EfiMemoryMappedIO)
        return L"APIC MapIO  ";
    if (Type == EfiMemoryMappedIOPortSpace)
        return L"APIC MapIOP ";
    if (Type == EfiPalCode)
        return L"Pal         ";
    if (Type == EfiMaxMemoryType)
        return L"Max         ";
    return L"Unknown     ";
}

void
ShowMemoryMap(MAYBE_UNUSED int argc, MAYBE_UNUSED void *arg)
{
    MM_INITIAL_MAP *map = GetLoaderRuntimeMemoryMap();
    if (!map)
    {
        ConsoleFormatWrite(L"Failed to get memory map: %k\r\n", EFI_OUT_OF_RESOURCES);
        return;
    }

    ConsoleFormatWrite(L"The Memory Map in this PC: \r\n");

    UINTN i, map_total_entries, map_descriptor_size;
    EFI_MEMORY_DESCRIPTOR *efi_memory_map = NULL;

    map_total_entries = map->DescriptorTotalSize / map->DescriptorSize;
    map_descriptor_size = map->DescriptorSize;
    efi_memory_map = map->Segs;

    for (i = 0; i < map_total_entries; i++)
    {
        EFI_PHYSICAL_ADDRESS physical_end;
        physical_end = efi_memory_map->PhysicalStart + efi_memory_map->NumberOfPages * 4096 - 1;

        // Format: Type : Start - End : Attribute
        // Assuming %016x is supported for zero-padding to 16 digits
        ConsoleFormatWrite(L"%s : %016x - %016x : %016x\r\n",
                           MemoryTypeString(efi_memory_map->Type),
                           efi_memory_map->PhysicalStart,
                           physical_end,
                           efi_memory_map->Attribute);

        efi_memory_map = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)efi_memory_map + map_descriptor_size);
    }

    ConsoleFormatWrite(L"Map Total Entries: %u\r\n", map_total_entries);

    g_ST->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)map, map->DescriptorTotalSize >> 12);
}

void
Boot(MAYBE_UNUSED int argc, MAYBE_UNUSED void *args)
{
    if (argc < 2)
    {
        ConsoleFormatWrite(L"Error: Boot kernel path not specified\r\n");
        ConsoleFormatWrite(L"Usage: boot <path to kernel file>\r\n");
        return;
    }

    COMMAND_STRING *kernelPath = FindCommandString(argc, args, MAX_ARGBUF_SIZ, 1);
    if (!kernelPath || kernelPath->Length == 0)
    {
        ConsoleFormatWrite(L"Error: Invalid kernel path\r\n");
        return;
    }
    StagingKernel(kernelPath->String);
}

void
Dir(MAYBE_UNUSED int argc, void *args)
{
    const uint64_t MAX_FILES = 256;
    FILE_INFO *files = NULL;
    uint64_t fileCount = 0;
    EFI_STATUS status;
    int result;
    COMMAND_STRING *dir = FindCommandString(argc, args, MAX_ARGBUF_SIZ, 1);

    if (!dir)
    {
        // No directory specified, use root
        dir = (COMMAND_STRING *)args;
        dir->Length = 0;
        dir->String[0] = L'\0';
    }

    status = g_ST->BootServices->AllocatePool(EfiBootServicesData, sizeof(FILE_INFO) * MAX_FILES, (void **)&files);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to allocate memory for file list: %k (0x%x)\r\n", status, status);
        return;
    }

    result = ListDir(dir->String, files, MAX_FILES, &fileCount);
    if (result != EC_SUCCESS)
    {
        ConsoleFormatWrite(L"Failed to list directory: 0x%x\r\n", result);
        g_ST->BootServices->FreePool(files);
        return;
    }

    UINT64 totalFileCount = 0;
    UINT64 totalDirCount = 0;
    UINT64 totalSize = 0;

    ConsoleFormatWrite(L" Directory of /%s:\r\n", dir->Length == 0 ? L"" : dir->String);
    ConsoleFormatWrite(L"Date       Time           Size/Type Name\r\n");
    ConsoleFormatWrite(L"---------- ----- ------------------ ----------------\r\n");

    for (uint64_t i = 0; i < fileCount; i++)
    {
        FILE_INFO *info = &files[i];

        // Date and Time: 01/02/2025 12:30
        ConsoleFormatWrite(L"%02u/%02u/%04u %02u:%02u ",
                           info->ModificationTime.Month,
                           info->ModificationTime.Day,
                           info->ModificationTime.Year,
                           info->ModificationTime.Hour,
                           info->ModificationTime.Minute);

        if (info->Attribute & EFI_FILE_DIRECTORY)
        {
            // <DIR> aligned to 19 chars (including trailing space)
            ConsoleFormatWrite(L"<DIR>              ");
            totalDirCount++;
        }
        else
        {
            // Size right aligned to 18 chars + 1 space
            ConsoleFormatWrite(L"%18u ", info->FileSize);

            totalFileCount++;
            totalSize += info->FileSize;
        }

        ConsoleFormatWrite(L"%s\r\n", info->FileName);
    }

    ConsoleFormatWrite(L"\r\n");
    ConsoleFormatWrite(L"        %u File(s)    %u byte\r\n", totalFileCount, totalSize);
    ConsoleFormatWrite(L"        %u Dir(s)\r\n", totalDirCount);

    g_ST->BootServices->FreePool(files);
}

int
ParseCommandLine(const CHAR16 *cmdline, void *buf, uint64_t bufSiz)
{
    uint64_t bufOffset = 0;
    int count = 0;
    const CHAR16 *p = cmdline;
    COMMAND_STRING *command = (COMMAND_STRING *)((uint8_t *)buf + bufOffset);

    command->Length = 0;
    while (1)
    {
        while (*p == L' ')
        {
            p++;
        }

        if (*p == L'\0')
        {
            break;
        }

        count++;
        command = (COMMAND_STRING *)((uint8_t *)buf + bufOffset);
        command->Length = 0;

        const CHAR16 *wordStart = p;
        while (*p != L' ' && *p != L'\0')
        {
            p++;
        }

        uint64_t wordLen = p - wordStart;

        uint64_t commandTotalSize = sizeof(COMMAND_STRING) + (wordLen + 1) * sizeof(CHAR16);

        if (bufOffset + commandTotalSize > bufSiz)
        {
            return -1; // Error: Buffer overflow
        }

        command->Length = wordLen;
        for (uint64_t i = 0; i < wordLen; ++i)
        {
            command->String[i] = wordStart[i];
        }
        command->String[wordLen] = L'\0';

        bufOffset += HO_ALIGN_UP(commandTotalSize, sizeof(uint64_t));

        if (*p == L'\0')
        {
            break;
        }
    }

    return count;
}

COMMAND_STRING *
NextCommandString(COMMAND_STRING *current, void *buf, uint64_t bufSiz)
{
    if (!current || !buf)
    {
        return NULL;
    }

    uint8_t *base = buf;
    uint8_t *ptr = (uint8_t *)current;
    uint64_t size = sizeof(COMMAND_STRING) + (current->Length + 1) * sizeof(CHAR16);
    uint8_t *next = ptr + HO_ALIGN_UP(size, sizeof(uint64_t));

    if (next >= base + bufSiz)
    {
        return NULL;
    }

    return (COMMAND_STRING *)next;
}

COMMAND_STRING *
FindCommandString(int argc, void *buf, uint64_t bufSiz, int index)
{
    if (!buf || index < 0 || index >= argc)
    {
        return NULL;
    }

    COMMAND_STRING *arg = buf;
    for (int i = 0; i < index; i++)
    {
        arg = NextCommandString(arg, buf, bufSiz);
        if (!arg)
        {
            return NULL;
        }
    }
    return arg;
}