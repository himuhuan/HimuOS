/**
 * HimuOperatingSystem
 *
 * File: ke/time/tsc_driver.h
 * Description:
 * TSC driver internal header.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <arch/arch.h>

BOOL TscDetectAvailable(const ARCH_BASIC_CPU_INFO *cpuInfo);
BOOL TscDetectInvariant(const ARCH_BASIC_CPU_INFO *cpuInfo);
uint64_t TscGetFreqFromCpuid(const ARCH_BASIC_CPU_INFO *cpuInfo);
uint64_t TscReadTick(void);
