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
        PRINT_HEX_WITH_MESSAGE("Allocate input buffer failed: ", st);
        return;
    }

    UINT8 *parseBuf = NULL;
    st = g_ST->BootServices->AllocatePool(EfiBootServicesData, MAX_ARGBUF_SIZ, (void **)&parseBuf);
    if (EFI_ERROR(st))
    {
        PRINT_HEX_WITH_MESSAGE("Allocate parse buffer failed: ", st);
        return;
    }

    while (TRUE)
    {
        ConsoleWriteStr(Prompt);
        if (ConsoleReadline(inputLineBuf, MAX_LINE) <= 0)
            continue;

        BOOL found = FALSE;
        for (int i = 0; g_command_table[i].CommandName != NULL; i++)
        {

            int argc = ParseCommandLine(inputLineBuf, parseBuf, MAX_ARGBUF_SIZ);
            if (argc < 0)
            {
                ConsoleWriteStr(TEXT("Error: command line too long\r\n"));
                found = TRUE; // Prevent "command not found" message
                break;
            }

            COMMAND_STRING *command = FindCommandString(argc, parseBuf, MAX_ARGBUF_SIZ, 0);
            if (!wstrcmp(command->String, g_command_table[i].CommandName))
            {
                if (EFI_ERROR(st))
                {
                    PRINT_HEX_WITH_MESSAGE("Allocate parse buffer failed: ", st);
                    break;
                }

                g_command_table[i].Function(argc, parseBuf);
                found = TRUE;
                break;
            }
        }

        if (!found)
        {
            ConsoleWriteStr(TEXT("Error: '"));
            ConsoleWriteStr(inputLineBuf);
            ConsoleWriteStr(TEXT("': command not found\r\n"));
        }
    }

    // Unreachable with current loop, but kept for completeness
    g_ST->BootServices->FreePool(inputLineBuf);
    g_ST->BootServices->FreePool(parseBuf);
}

// Helper function to convert a number to hexadecimal string
void
UInt64ToHexStr(UINT64 value, CHAR16 *buffer, int width)
{
    static const CHAR16 hex_chars[16] = {TEXT('0'), TEXT('1'), TEXT('2'), TEXT('3'), TEXT('4'), TEXT('5'),
                                         TEXT('6'), TEXT('7'), TEXT('8'), TEXT('9'), TEXT('A'), TEXT('B'),
                                         TEXT('C'), TEXT('D'), TEXT('E'), TEXT('F')};
    int i;

    for (i = 0; i < width; i++)
    {
        buffer[i] = TEXT('0');
    }
    buffer[width] = TEXT('\0');

    i = width - 1;
    while (value > 0 && i >= 0)
    {
        buffer[i] = hex_chars[value & 0xF];
        value >>= 4;
        i--;
    }
}

CHAR16 *
MemoryTypeString(UINT32 Type)
{
    if (Type == EfiReservedMemoryType)
        return L"Reserved    ";
    if (Type == EfiLoaderCode)
        return L"LoderCode   ";
    if (Type == EfiLoaderData)
        return L"LoderData   ";
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
PadHexLL(CHAR16 *hex_buffer)
{
    UINTN length, offset;
    CHAR16 buffer[17] = {'0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', 0};

    length = 0;
    while (hex_buffer[length] != TEXT('\0'))
    {
        length++;
    }

    offset = 16 - length;
    memcpy(buffer + offset, hex_buffer, length * sizeof(CHAR16));
    memcpy(hex_buffer, buffer, sizeof(buffer));
}

void
ShowMemoryMap(MAYBE_UNUSED int argc, MAYBE_UNUSED void *arg)
{
    MM_INITIAL_MAP *map = GetLoaderRuntimeMemoryMap();
    if (!map)
    {
        PRINT_HEX_WITH_MESSAGE("Failed to get memory map: ", EFI_OUT_OF_RESOURCES);
        return;
    }

    ConsoleWriteStr(TEXT("The Memory Map in this PC: \r\n"));

    UINTN i, map_total_entries, map_descriptor_size;
    EFI_MEMORY_DESCRIPTOR *efi_memory_map = NULL;

    map_total_entries = map->DescriptorTotalSize / map->DescriptorSize;
    map_descriptor_size = map->DescriptorSize;
    efi_memory_map = map->Segs;

    for (i = 0; i < map_total_entries; i++)
    {
        CHAR16 hex_buffer[30];
        EFI_PHYSICAL_ADDRESS physical_end;

        ConsoleWriteStr(MemoryTypeString(efi_memory_map->Type));
        ConsoleWriteStr(L" : ");

        memset(hex_buffer, 0, sizeof(hex_buffer));
        UInt64ToHexStr(efi_memory_map->PhysicalStart, hex_buffer, 16);
        PadHexLL(hex_buffer);
        ConsoleWriteStr(hex_buffer);
        ConsoleWriteStr(L" - ");

        physical_end = efi_memory_map->PhysicalStart + efi_memory_map->NumberOfPages * 4096 - 1;
        memset(hex_buffer, 0, sizeof(hex_buffer));
        UInt64ToHexStr(physical_end, hex_buffer, 16);
        PadHexLL(hex_buffer);
        ConsoleWriteStr(hex_buffer);
        ConsoleWriteStr(L" : ");

        memset(hex_buffer, 0, sizeof(hex_buffer));
        UInt64ToHexStr(efi_memory_map->Attribute, hex_buffer, 16);
        PadHexLL(hex_buffer);
        ConsoleWriteStr(hex_buffer);
        ConsoleWriteStr(L"\r\n");
        efi_memory_map = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)efi_memory_map + map_descriptor_size);
    }

    PRINT_HEX_WITH_MESSAGE("Map Total Entries: ", map_total_entries);

    g_ST->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)map, map->DescriptorTotalSize >> 12);
}

void
Boot(MAYBE_UNUSED int argc, MAYBE_UNUSED void *args)
{
    if (argc < 2)
    {
        ConsoleWriteStr(L"Error: Boot kernel path not specified\r\n");
        ConsoleWriteStr(L"Usage: boot <path to kernel file>\r\n");
        return;
    }

    COMMAND_STRING *kernelPath = FindCommandString(argc, args, MAX_ARGBUF_SIZ, 1);
    if (!kernelPath || kernelPath->Length == 0)
    {
        ConsoleWriteStr(L"Error: Invalid kernel path\r\n");
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
        PRINT_HEX_WITH_MESSAGE("Failed to allocate memory for file list: ", status);
        return;
    }

    result = ListDir(dir->String, files, MAX_FILES, &fileCount);
    if (result != EC_SUCCESS)
    {
        PRINT_HEX_WITH_MESSAGE("Failed to list directory: ", result);
        g_ST->BootServices->FreePool(files);
        return;
    }

    CHAR16 formatBuffer[21];
    UINT64 totalFileCount = 0;
    UINT64 totalDirCount = 0;
    UINT64 totalSize = 0;

    ConsoleWriteStr(L" Directory of /");
    ConsoleWriteStr(dir->Length == 0 ? L"" : dir->String);
    ConsoleWriteStr(L":\r\n");
    ConsoleWriteStr(L"Date       Time           Size/Type Name\r\n");
    ConsoleWriteStr(L"---------- ----- ------------------ ----------------\r\n");

    for (uint64_t i = 0; i < fileCount; i++)
    {
        FILE_INFO *info = &files[i];

        if (info->ModificationTime.Month < 10)
            ConsoleWriteStr(L"0");
        FormatUInt64(info->ModificationTime.Month, formatBuffer);
        ConsoleWriteStr(formatBuffer);
        ConsoleWriteStr(L"/");
        if (info->ModificationTime.Day < 10)
            ConsoleWriteStr(L"0");
        FormatUInt64(info->ModificationTime.Day, formatBuffer);
        ConsoleWriteStr(formatBuffer);
        ConsoleWriteStr(L"/");
        FormatUInt64(info->ModificationTime.Year, formatBuffer);
        ConsoleWriteStr(formatBuffer);
        ConsoleWriteStr(L" ");

        if (info->ModificationTime.Hour < 10)
            ConsoleWriteStr(L"0");
        FormatUInt64(info->ModificationTime.Hour, formatBuffer);
        ConsoleWriteStr(formatBuffer);
        ConsoleWriteStr(L":");
        if (info->ModificationTime.Minute < 10)
            ConsoleWriteStr(L"0");
        FormatUInt64(info->ModificationTime.Minute, formatBuffer);
        ConsoleWriteStr(formatBuffer);
        ConsoleWriteStr(L" ");

        if (info->Attribute & EFI_FILE_DIRECTORY)
        {
            ConsoleWriteStr(L"<DIR>              ");
            totalDirCount++;
        }
        else
        {
            UINTN num_digits = 0;
            UINT64 temp_size = info->FileSize;
            if (temp_size == 0)
            {
                num_digits = 1;
            }
            else
            {
                while (temp_size > 0)
                {
                    temp_size /= 10;
                    num_digits++;
                }
            }

            for (UINTN j = 0; j < 18 - num_digits; ++j)
            {
                ConsoleWriteStr(L" ");
            }

            FormatUInt64(info->FileSize, formatBuffer);
            ConsoleWriteStr(formatBuffer);
            ConsoleWriteStr(L" ");

            totalFileCount++;
            totalSize += info->FileSize;
        }

        ConsoleWriteStr(info->FileName);
        ConsoleWriteStr(L"\r\n");
    }

    ConsoleWriteStr(L"\r\n");
    ConsoleWriteStr(L"        ");
    FormatUInt64(totalFileCount, formatBuffer);
    ConsoleWriteStr(formatBuffer);
    ConsoleWriteStr(L" File(s)    ");
    FormatAsStorageUnit(totalSize, formatBuffer);
    ConsoleWriteStr(formatBuffer);
    ConsoleWriteStr(L"\r\n");

    ConsoleWriteStr(L"        ");
    FormatUInt64(totalDirCount, formatBuffer);
    ConsoleWriteStr(formatBuffer);
    ConsoleWriteStr(L" Dir(s)\r\n");

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