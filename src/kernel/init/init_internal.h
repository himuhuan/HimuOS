/**
 * HimuOperatingSystem
 *
 * File: init/init_internal.h
 * Description: Private declarations shared by kernel init modules.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/init.h>
#include <drivers/serial.h>
#include <kernel/hodbg.h>
#include <arch/amd64/idt.h>
#include <arch/amd64/acpi.h>
#include <arch/amd64/efi_mem.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/time_source.h>
#include <kernel/ke/clock_event.h>
#include <kernel/ke/pool.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/scheduler.h>
#include "assets/fonts/font8x16.h"

void InitBitmapFont(void);
void InitCpuState(STAGING_BLOCK *block);
void VerifyHhdm(STAGING_BLOCK *block);
void AssertRsdp(HO_VIRTUAL_ADDRESS rsdpVirt);
const BOOT_MAPPING_MANIFEST_HEADER *ValidateBootMappingManifest(STAGING_BLOCK *block);
