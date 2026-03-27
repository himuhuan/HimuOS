/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo_cpu.c
 * Description: CPU and descriptor table query handlers.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"
#include <kernel/ke/sysinfo.h>
#include <kernel/hodefs.h>
#include <arch/arch.h>
#include <libc/string.h>

// ─────────────────────────────────────────────────────────────
// Inline assembly helpers
// ─────────────────────────────────────────────────────────────

static inline void
StorGdt(GDT_PTR *gdtPtr)
{
    __asm__ volatile("sgdt %0" : "=m"(*gdtPtr));
}

typedef struct
{
    uint16_t Limit;
    uint64_t Base;
} __attribute__((packed)) IDT_PTR;

static inline void
StorIdt(IDT_PTR *idtPtr)
{
    __asm__ volatile("sidt %0" : "=m"(*idtPtr));
}

static inline void
Cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(subleaf));
}

// ─────────────────────────────────────────────────────────────
// CPU queries
// ─────────────────────────────────────────────────────────────

HO_STATUS
QueryCpuBasic(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(ARCH_BASIC_CPU_INFO);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    GetBasicCpuInfo((ARCH_BASIC_CPU_INFO *)Buffer);
    return EC_SUCCESS;
}

HO_STATUS
QueryCpuFeatures(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_CPU_FEATURES);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_CPU_FEATURES *info = (SYSINFO_CPU_FEATURES *)Buffer;
    uint32_t eax, ebx, ecx, edx;

    Cpuid(0x01, 0, &eax, &ebx, &ecx, &edx);
    info->Leaf1_ECX = ecx;
    info->Leaf1_EDX = edx;

    Cpuid(0x07, 0, &eax, &ebx, &ecx, &edx);
    info->Leaf7_EBX = ebx;
    info->Leaf7_ECX = ecx;

    Cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
    info->ExtLeaf1_ECX = ecx;
    info->ExtLeaf1_EDX = edx;

    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// Descriptor table queries
// ─────────────────────────────────────────────────────────────

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

    GDT_PTR gdtPtr;
    StorGdt(&gdtPtr);

    TSS_DESCRIPTOR *tssDesc = (TSS_DESCRIPTOR *)((uint8_t *)gdtPtr.Base + GDT_TSS_INDEX * sizeof(GDT_ENTRY));

    uint64_t tssBase = (uint64_t)tssDesc->BaseLow | ((uint64_t)tssDesc->BaseMiddle << 16) |
                       ((uint64_t)tssDesc->BaseHigh << 24) | ((uint64_t)tssDesc->BaseUpper << 32);

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
