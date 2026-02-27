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
#include <kernel/ke/time_source.h>
#include <kernel/ke/clock_event.h>

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

    HO_STATUS status = KeClockEventSetNextEvent(1000000000ULL);
    if (status != EC_SUCCESS)
    {
        HO_KPANIC(status, "Failed to arm first clock event");
    }

    uint64_t printedSeconds = 0;
    while (TRUE)
    {
        __asm__ __volatile__("sti; hlt" ::: "memory");

        uint64_t interruptCount = KeClockEventGetInterruptCount();
        while (printedSeconds < interruptCount)
        {
            printedSeconds++;
            klog(KLOG_LEVEL_INFO, "[TICK] %lu sec passed!\n", printedSeconds);

            status = KeClockEventSetNextEvent(1000000000ULL);
            if (status != EC_SUCCESS)
            {
                HO_KPANIC(status, "Failed to arm next clock event");
            }
        }
    }
}
