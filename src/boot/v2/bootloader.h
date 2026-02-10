/**
 * HimuOperatingSystem
 *
 * File: bootloader.h
 * Description: This file contains the definition of the bootloader interface.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "arch/amd64/efi_min.h"
#include "kernel/hodefs.h"

extern struct EFI_SYSTEM_TABLE *g_ST;
extern struct EFI_GRAPHICS_OUTPUT_PROTOCOL *g_GOP;
extern struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *g_FSP;
extern EFI_HANDLE gImageHandle;

void LoaderInitialize(EFI_HANDLE imageHandle, struct EFI_SYSTEM_TABLE *systemTable);

EFI_STATUS StagingKernel(const CHAR16 *path);

BOOL BootUseNx(void);
