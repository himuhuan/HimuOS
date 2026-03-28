/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo/memory.c
 * Description:
 * Memory-oriented system information query handlers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"

#if __HO_DEBUG_BUILD__
HO_STATUS
QueryBootMemoryMap(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    BOOT_CAPSULE *capsule = KeGetBootCapsule();
    if (!capsule)
        return EC_INVALID_STATE;

    size_t required = capsule->Layout.MemoryMapSize;

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    HO_VIRTUAL_ADDRESS mapVirt = HHDM_BASE_VA + capsule->MemoryMapPhys;
    memcpy(Buffer, (void *)mapVirt, required);

    return EC_SUCCESS;
}
#endif

HO_STATUS
QueryPageTable(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_PAGE_TABLE);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_PAGE_TABLE *info = (SYSINFO_PAGE_TABLE *)Buffer;
    info->Cr3 = ReadCr3();

    return EC_SUCCESS;
}

HO_STATUS
QueryPhysicalMemStats(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    (void)Buffer;
    (void)BufferSize;
    (void)RequiredSize;
    // Reserved for PMM implementation
    return EC_NOT_SUPPORTED;
}

HO_STATUS
QueryVirtualLayout(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_VIRTUAL_LAYOUT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_VIRTUAL_LAYOUT *info = (SYSINFO_VIRTUAL_LAYOUT *)Buffer;
    info->KernelBase = KRNL_BASE_VA;
    info->KernelStack = KRNL_STACK_VA;
    info->HhdmBase = HHDM_BASE_VA;
    info->MmioBase = MMIO_BASE_VA;

    return EC_SUCCESS;
}
