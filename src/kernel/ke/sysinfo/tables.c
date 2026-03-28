/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo/tables.c
 * Description:
 * Descriptor-table system information query handlers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"

HO_STATUS
QueryGdt(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_GDT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_GDT *info = (SYSINFO_GDT *)Buffer;

    GDT_PTR gdtPtr;
    StorGdt(&gdtPtr);

    info->Limit = gdtPtr.Limit;
    info->Base = gdtPtr.Base;
    info->EntryCount = NGDT;

    // Copy GDT entries
    GDT_ENTRY *gdtBase = (GDT_ENTRY *)gdtPtr.Base;
    for (uint16_t i = 0; i < NGDT; i++)
    {
        info->Entries[i] = gdtBase[i];
    }

    return EC_SUCCESS;
}

HO_STATUS
QueryTss(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(TSS64);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    // Get GDT base to find TSS descriptor
    GDT_PTR gdtPtr;
    StorGdt(&gdtPtr);

    // TSS descriptor is at index 5 (16 bytes, spans entries 5-6)
    TSS_DESCRIPTOR *tssDesc = (TSS_DESCRIPTOR *)((uint8_t *)gdtPtr.Base + GDT_TSS_INDEX * sizeof(GDT_ENTRY));

    // Reconstruct TSS base address from descriptor
    uint64_t tssBase = (uint64_t)tssDesc->BaseLow | ((uint64_t)tssDesc->BaseMiddle << 16) |
                       ((uint64_t)tssDesc->BaseHigh << 24) | ((uint64_t)tssDesc->BaseUpper << 32);

    // Copy TSS
    memcpy(Buffer, (void *)tssBase, sizeof(TSS64));

    return EC_SUCCESS;
}

HO_STATUS
QueryIdt(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_IDT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_IDT *info = (SYSINFO_IDT *)Buffer;

    IDT_PTR idtPtr;
    StorIdt(&idtPtr);

    info->Limit = idtPtr.Limit;
    info->Base = idtPtr.Base;

    return EC_SUCCESS;
}
