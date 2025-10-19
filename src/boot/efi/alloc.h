/**
 * HimuOperatingSystem
 *
 * File: alloc.h
 * Module: Boot
 * Description: A simple BUMP memory allocator in UEFI environment
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "efi.h"

/* HimuOs Boot Bump Allocator */
typedef struct _HOB_BALLOC
{
    UINTN Base;
    UINTN TotalSize;
    UINTN Offset;
    EFI_STATUS ErrorCode;
} HOB_BALLOC;

HO_KERNEL_API HO_NODISCARD EFI_STATUS HobAllocCreate(UINT64 baseAddress, HOB_BALLOC *allocator, UINTN nPage);

/* HobAlloc always promises zero-initialized memory */
HO_KERNEL_API HO_NODISCARD void *HobAlloc(HOB_BALLOC *allocator, UINTN size, UINTN align);
HO_KERNEL_API HO_NODISCARD UINTN HobRemaining(const HOB_BALLOC *allocator);
HO_KERNEL_API HO_NODISCARD BOOL HobAllocError(const HOB_BALLOC *allocator);
