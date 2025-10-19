/**
 * HimuOperatingSystem
 *
 * File: mm_efi_defs.h
 * Description:
 * This header defines the memory management structures and constants
 * used in the EFI (Extensible Firmware Interface) environment.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "libc/stddef.h"

typedef enum
{
    EfiReservedMemoryType = 0,
    EfiLoaderCode = 1,
    EfiLoaderData = 2,
    EfiBootServicesCode = 3,
    EfiBootServicesData = 4,
    EfiRuntimeServicesCode = 5,
    EfiRuntimeServicesData = 6,
    EfiConventionalMemory = 7,
    EfiUnusableMemory = 8,
    EfiACPIReclaimMemory = 9,
    EfiACPIMemoryNVS = 10,
    EfiMemoryMappedIO = 11,
    EfiMemoryMappedIOPortSpace = 12,
    EfiPalCode = 13,
    EfiPersistentMemory = 14,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

/*  In different platform, `EFI_MEMORY_DESCRIPTOR` may have different size, but always
    has the following members with specified layout.
    It's recommended that use `p = (struct EFI_MEMORY_DESCRIPTOR *)((UINT8 *)memory_map + i * descriptor_size);` rather
   than `p += 1` or `p[i]`  */
typedef struct _EFI_MEMORY_DESCRIPTOR
{
    uint32_t Type;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR, *PEFI_MEMORY_DESCRIPTOR;

typedef uint64_t EFI_PHYSICAL_ADDRESS;

typedef uint64_t EFI_VIRTUAL_ADDRESS;

typedef struct _MM_INITIAL_MAP
{
    uint64_t Size;
    uint64_t MemoryMapKey;
    uint64_t DescriptorSize;
    uint32_t DescriptorVersion;
    uint64_t DescriptorTotalSize;
    EFI_MEMORY_DESCRIPTOR Segs[];
} __attribute__((aligned(8))) MM_INITIAL_MAP, *PMM_INITIAL_MAP;

HO_NODISCARD BOOL IsUsableMemory(uint32_t type);

/**
 * Create an initial memory map structure on the given memory area.
 */
MM_INITIAL_MAP *InitMemoryMap(void *base, size_t size);