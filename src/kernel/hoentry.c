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
#include <kernel/ke/sysinfo.h>

void kmain(BOOT_CAPSULE *capsule);

static void PrintBootBanner(void);
void
kmain(BOOT_CAPSULE *capsule)
{
    InitKernel(capsule);
    PrintBootBanner();

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

static void
PrintBootBanner(void)
{
    ARCH_BASIC_CPU_INFO cpu;
    SYSINFO_SYSTEM_VERSION ver;

    if (KeQuerySystemInformation(KE_SYSINFO_CPU_BASIC, &cpu, sizeof(cpu), NULL) == EC_SUCCESS)
    {
        kprintf("CPU:                     %s (x86_64)\n", cpu.ModelName);
        kprintf("Timer Features\n");
        kprintf(" * Counter:              %s\n", cpu.TimerFeatures & ARCH_TIMER_FEAT_COUNTER ? "YES" : "NOT SUPPORTED");
        kprintf(" * Invariant counter:    %s\n",
                cpu.TimerFeatures & ARCH_TIMER_FEAT_INVARIANT ? "YES" : "NOT SUPPORTED");
        kprintf(" * Deadline mode:        %s\n\n",
                cpu.TimerFeatures & ARCH_TIMER_FEAT_ONE_SHOT ? "YES" : "NOT SUPPORTED");
    }

    if (KeQuerySystemInformation(KE_SYSINFO_SYSTEM_VERSION, &ver, sizeof(ver), NULL) == EC_SUCCESS)
    {
        kprintf("Himu Operating System VERSION %u.%u.%u %s %s\n", ver.Major, ver.Minor, ver.Patch, ver.BuildDate,
                ver.BuildTime);
    }

    kprintf("Copyright(c) 2024-2025 Himu, ONLY FOR EDUCATIONAL PURPOSES.\n\n");
}
