/**
 * HimuOperatingSystem
 *
 * File: uefi_main.c
 * Description: Main entry point for UEFI boot manager
 * Module: boot
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "efi.h"
#include "io.h"
#include "shell.h"
#include "bootloader.h"

void
efi_main(void *ImageHandle, struct EFI_SYSTEM_TABLE *SystemTable)
{
    SystemTable->ConsoleOutput->ClearScreen(SystemTable->ConsoleOutput);
    EfiInitialize(ImageHandle, SystemTable);
    ConsoleWriteStr(L"Himu Operating System UEFI Boot Manager, Version 1.1.3\r\n");
    PRINT_HEX_WITH_MESSAGE(">>> UEFI loader started at ", (UINT64)efi_main);
    PRINT_HEX_WITH_MESSAGE(">>> StagingKernel at ", (UINT64)StagingKernel);
    StagingKernel(TEXT("kernel.bin"));
}