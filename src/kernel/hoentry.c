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
#include <kernel/ke/scheduler.h>
#include <kernel/demo.h>
#include "demo/demo_internal.h"

static void PrintBootBanner(void);

void kmain(BOOT_CAPSULE *capsule);

// ─────────────────────────────────────────────────────────────
// Kernel entry point (MUST be the first function in this file)
// ─────────────────────────────────────────────────────────────

void
kmain(BOOT_CAPSULE *capsule)
{
    InitKernel(capsule);
    PrintBootBanner();

    RunKernelDemos();

    // Query & print initial scheduler state
    KE_SYSINFO_SCHEDULER_DATA schedInfo;
    if (KeQuerySchedulerInfo(&schedInfo) == EC_SUCCESS)
    {
        klog(KLOG_LEVEL_INFO, "[SCHED] enabled=%u idle=%u active=%u ready=%u\n", schedInfo.SchedulerEnabled,
             schedInfo.IdleThreadId, schedInfo.ActiveThreadCount, schedInfo.ReadyQueueDepth);
    }

    // Current execution flow IS the IdleThread — enter idle loop
    KeIdleLoop();
    // Never reached
}

static void
PrintBootBanner(void)
{
    ARCH_BASIC_CPU_INFO cpu;
    SYSINFO_SYSTEM_VERSION ver;

    kprintf("  %-24s %s\n", "------------------------", "------------------------------------------");
    if (KeQuerySystemInformation(KE_SYSINFO_CPU_BASIC, &cpu, sizeof(cpu), NULL) == EC_SUCCESS)
    {
        kprintf("  %-24s %s (x86_64)\n", "CPU", cpu.ModelName);
        kprintf("  %-24s %s\n", "TSC Counter",
                cpu.TimerFeatures & ARCH_TIMER_FEAT_COUNTER ? "Supported" : "Not Supported");
        kprintf("  %-24s %s\n", "Invariant TSC",
                cpu.TimerFeatures & ARCH_TIMER_FEAT_INVARIANT ? "Supported" : "Not Supported");
        kprintf("  %-24s %s\n", "TSC Deadline Mode",
                cpu.TimerFeatures & ARCH_TIMER_FEAT_ONE_SHOT ? "Supported" : "Not Supported");
    }

    if (KeQuerySystemInformation(KE_SYSINFO_SYSTEM_VERSION, &ver, sizeof(ver), NULL) == EC_SUCCESS)
    {
        kprintf("  %-24s %u.%u.%u\n", "Version", ver.Major, ver.Minor, ver.Patch);
        kprintf("  %-24s %s %s\n", "Build", ver.BuildDate, ver.BuildTime);
    }
    kprintf("  %-24s %s\n", "------------------------", "------------------------------------------");
    kprintf("  %-24s %s\n", "Active Profile", HO_DEMO_TEST_SELECTION_NAME);

    kprintf("Copyright(c) 2024-2025 Himu, ONLY FOR EDUCATIONAL PURPOSES.\n\n");
}
