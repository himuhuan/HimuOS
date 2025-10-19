/**
 * HimuOperatingSystem
 *
 * File: cpu.h
 * Description: AMD64 CPU architecture definitions.
 * Copyright(c) 2024-2025 HimuOperatingSystem, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

/**
 * Halt the CPU indefinitely.
 */
HO_NORETURN void HO_KERNEL_API Halt(void);