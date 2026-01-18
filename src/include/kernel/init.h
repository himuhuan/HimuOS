/**
 * HimuOperatingSystem
 *
 * File: init.h
 * Description:
 * Initialization functions for the HimuOperatingSystem kernel.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include <boot/boot_capsule.h>
#include <kernel/console.h>
#include <kernel/mm/mm.h>
#include <arch/arch.h>

extern KE_VIDEO_DRIVER gVideoDriver;
extern ARCH_BASIC_CPU_INFO gBasicCpuInfo;
extern BITMAP_FONT_INFO gSystemFont;


void InitKernel(BOOT_CAPSULE *block);