/**
 * HimuOperatingSystem
 *
 * File: ke/time/tsc_driver.c
 * Description:
 * TSC (Time Stamp Counter) driver for x64.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "tsc_driver.h"
#include <arch/amd64/asm.h>
#include <arch/arch.h>

static uint64_t
ReadTickInternal(void)
{
    return rdtsc();
}

BOOL
TscDetectAvailable(const ARCH_BASIC_CPU_INFO *cpuInfo)
{
    return (cpuInfo->TimerFeatures & ARCH_TIMER_FEAT_COUNTER) != 0;
}

BOOL
TscDetectInvariant(const ARCH_BASIC_CPU_INFO *cpuInfo)
{
    return (cpuInfo->TimerFeatures & ARCH_TIMER_FEAT_INVARIANT) != 0;
}

uint64_t
TscGetFreqFromCpuid(const ARCH_BASIC_CPU_INFO *cpuInfo)
{
    uint32_t eax, ebx, ecx, edx;

    // Try CPUID.15H first (TSC/Core Crystal Clock ratio)
    if (cpuInfo->SpecificInfo.X64.MaxLeafSupported >= 0x15)
    {
        cpuidex(0x15, 0, &eax, &ebx, &ecx, &edx);
        // eax = denominator, ebx = numerator, ecx = crystal freq (Hz)
        if (eax != 0 && ebx != 0 && ecx != 0)
        {
            return ((uint64_t)ecx * ebx) / eax;
        }
    }

    // Try CPUID.16H (Processor Base Frequency in MHz)
    if (cpuInfo->SpecificInfo.X64.MaxLeafSupported >= 0x16)
    {
        cpuidex(0x16, 0, &eax, &ebx, &ecx, &edx);
        // eax = base freq in MHz
        if ((eax & 0xFFFF) != 0)
        {
            return (uint64_t)(eax & 0xFFFF) * 1000000ULL;
        }
    }

    return 0; // Frequency not available, needs calibration
}

uint64_t
TscReadTick(void)
{
    return ReadTickInternal();
}
