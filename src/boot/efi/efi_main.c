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

#define BOOTLOADER_VERSTR L"1.2.0"

void
efi_main(void *ImageHandle, struct EFI_SYSTEM_TABLE *SystemTable)
{
    SystemTable->ConsoleOutput->ClearScreen(SystemTable->ConsoleOutput);
    EfiInitialize(ImageHandle, SystemTable);
    ConsoleFormatWrite(L"Himu Operating System UEFI Boot Manager, Version %s\r\n", BOOTLOADER_VERSTR);
    ConsoleFormatWrite(L">>> UEFI loader started at %p\r\n", (UINT64)efi_main);
    Shell(L"HimuOS~BL# ");
    StagingKernel(TEXT("kernel.bin"));
}