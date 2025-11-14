/**
 * HimuOperatingSystem
 *
 * File: cpu.h
 * Description: AMD64 CPU architecture definitions.
 * Copyright(c) 2024-2025 HimuOperatingSystem, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include <arch/arch.h>

HO_NORETURN HO_KERNEL_API void x64_Halt(void);

HO_KERNEL_API void x64_GetBasicCpuInfo(ARCH_BASIC_CPU_INFO *info);