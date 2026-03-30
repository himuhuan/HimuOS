/**
 * HimuOperatingSystem
 *
 * File: init/cpu.c
 * Description: CPU-specific kernel initialization helpers.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "init_internal.h"

void
InitCpuState(STAGING_BLOCK *block)
{
    CPU_CORE_LOCAL_DATA *data = &block->CpuInfo;
    data->Tss.RSP0 = HHDM_PHYS2VIRT(block->KrnlStackPhys) + block->Layout.KrnlStackSize;
    data->Tss.IST1 = HHDM_PHYS2VIRT(block->KrnlIST1StackPhys) + block->Layout.IST1StackSize;
    data->Tss.IST2 = HHDM_PHYS2VIRT(block->KrnlIST2StackPhys) + block->Layout.IST2StackSize;
    data->Tss.IOMapBase = sizeof(TSS64); // No IO permission bitmap
}
