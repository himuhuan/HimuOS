/**
 * HimuOperatingSystem
 *
 * File: hoentry.c
 * Description: The entry point for the kernel.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <boot/boot_capsule.h>
#include <kernel/hodbg.h>
#include <kernel/init.h>

void kmain(BOOT_CAPSULE *capsule);

void
kmain(BOOT_CAPSULE *capsule)
{
    InitKernel(capsule);
    kprintf(ANSI_FG_GREEN "HimuOS Kernel Initialized Successfully!\n" ANSI_RESET);

    kprintf("CPU:                     %s (x86_64)\n", gBasicCpuInfo.ModelName);
    kprintf("Timer Features\n");
    kprintf(" * Counter:              %s\n",
            gBasicCpuInfo.TimerFeatures & ARCH_TIMER_FEAT_COUNTER ? "YES" : "NOT SUPPORTED");
    kprintf(" * Invariant counter:    %s\n",
            gBasicCpuInfo.TimerFeatures & ARCH_TIMER_FEAT_INVARIANT ? "YES" : "NOT SUPPORTED");
    kprintf(" * Deadline mode:        %s\n\n",
            gBasicCpuInfo.TimerFeatures & ARCH_TIMER_FEAT_ONE_SHOT ? "YES" : "NOT SUPPORTED");

    kprintf("Himu Operating System VERSION %s\n", KRNL_VERSTR);
    kprintf("Copyright(c) 2024-2025 Himu, ONLY FOR EDUCATIONAL PURPOSES.\n\n");

    while (1)
        ;
}
