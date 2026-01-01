/**
 * HimuOperatingSystem
 *
 * File: efi_mem.h
 * Description:
 * EFI memory-related definitions used in boot protocols.
 *
 * Only for AMD64 architecture.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "efi_min.h"

typedef struct EFI_MEMORY_MAP
{
    size_t Size;
    uint64_t MemoryMapKey;
    size_t DescriptorSize;
    uint32_t DescriptorVersion;
    size_t DescriptorTotalSize;
    size_t DescriptorCount;
    EFI_MEMORY_DESCRIPTOR Segs[];
} __attribute__((aligned(8))) EFI_MEMORY_MAP;

#define IS_RECLAIMABLE_MEMORY(type)                                                                                    \
    ((type) == EfiLoaderCode || (type) == EfiLoaderData || (type) == EfiBootServicesCode ||                            \
     (type) == EfiBootServicesData || (type) == EfiConventionalMemory)


