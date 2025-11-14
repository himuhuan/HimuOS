/**
 * HimuOperatingSystem
 *
 * File: arch.h
 * Description: Unified architecture header file.
 * Module: arch
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

#define ARCH_TIMER_FEAT_NONE      (0)      // No special timer features -- HimuOS do NOT expect it! :(
#define ARCH_TIMER_FEAT_COUNTER   (1 << 0) // Time Stamp Counter (TSC) is available
#define ARCH_TIMER_FEAT_INVARIANT (1 << 1) // Counter is invariant across frequency changes
#define ARCH_TIMER_FEAT_ONE_SHOT  (1 << 2) // Supports one-shot mode

#define CPU_MODEL_NAME_LENGTH     128

typedef struct ARCH_BASIC_CPU_INFO
{
    BOOL IsRunningInHypervisor;            // Indicates if running under a hypervisor
    uint32_t TimerFeatures;                // Timer feature flags
    char ModelName[CPU_MODEL_NAME_LENGTH]; // CPU model name string

    union {
        struct
        {
            uint32_t MaxLeafSupported;    // Maximum supported CPUID leaf
            uint32_t MaxExtLeafSupported; // Maximum supported extended CPUID leaf
        } X64;
    } SpecificInfo;
} ARCH_BASIC_CPU_INFO;

/**
 * Halt the CPU indefinitely.
 */
HO_NORETURN void HO_KERNEL_API Halt(void);

void HO_PUBLIC_API GetBasicCpuInfo(ARCH_BASIC_CPU_INFO *info);