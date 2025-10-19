/**
 * HimuOperatingSystem
 * Copyright(c) 2024-2025 Himu, ONLY FOR EDUCATIONAL PURPOSES.
 *
 * This is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * File: mm.h
 * Description: This header defines the memory management structures and constants
 * used in the kernel memory management (MM) subsystem.
 *
 */

#include "_hobase.h"
#include "kernel/mm/mm_efi.h"
#include "libc/string.h"

HO_NODISCARD BOOL
IsUsableMemory(uint32_t type)
{
    return type == EfiConventionalMemory || type == EfiBootServicesCode || type == EfiBootServicesData;
}

MM_INITIAL_MAP *
InitMemoryMap(void *base, size_t size)
{
    MM_INITIAL_MAP *map = (MM_INITIAL_MAP *)base;
    memset(base, 0, size);

    map->Size = size;
    map->DescriptorTotalSize = size - sizeof(MM_INITIAL_MAP);
    return map;
}
