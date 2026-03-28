/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo/cpu.c
 * Description:
 * CPU-oriented system information query handlers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"

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

    // CPUID.01H
    Cpuid(0x01, 0, &eax, &ebx, &ecx, &edx);
    info->Leaf1_ECX = ecx;
    info->Leaf1_EDX = edx;

    // CPUID.07H
    Cpuid(0x07, 0, &eax, &ebx, &ecx, &edx);
    info->Leaf7_EBX = ebx;
    info->Leaf7_ECX = ecx;

    // CPUID.80000001H
    Cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
    info->ExtLeaf1_ECX = ecx;
    info->ExtLeaf1_EDX = edx;

    return EC_SUCCESS;
}
