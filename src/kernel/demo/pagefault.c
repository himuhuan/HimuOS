/**
 * HimuOperatingSystem
 *
 * File: demo/pagefault.c
 * Description: Page-fault observability demos.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"
#include <kernel/ke/mm.h>

static volatile uint64_t gPageFaultImportedTarget = 0x5046494D504F5254ULL;

static void
StartPageFaultDemo(KTHREAD_ENTRY entry, const char *name)
{
    HO_STATUS status;
    KTHREAD *faultThread = NULL;

    status = KeThreadCreate(&faultThread, entry, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, name);

    status = KeThreadStart(faultThread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, name);
}

static void
TriggerNxExecuteFault(HO_VIRTUAL_ADDRESS faultVa)
{
    klog(KLOG_LEVEL_INFO, "[PF-DEMO] triggering NX execute fault at %p\n", (void *)(uint64_t)faultVa);
    __asm__ volatile("call *%0" : : "r"((uint64_t)faultVa));
}

void
RunPageFaultImportedDemo(void)
{
    StartPageFaultDemo(PageFaultImportedThread, "Failed to start imported-region page-fault demo");
}

void
RunPageFaultGuardDemo(void)
{
    StartPageFaultDemo(PageFaultGuardThread, "Failed to start guard-page page-fault demo");
}

void
RunPageFaultFixmapDemo(void)
{
    StartPageFaultDemo(PageFaultFixmapThread, "Failed to start fixmap page-fault demo");
}

void
RunPageFaultHeapDemo(void)
{
    StartPageFaultDemo(PageFaultHeapThread, "Failed to start heap page-fault demo");
}

void
PageFaultImportedThread(void *arg)
{
    (void)arg;
    TriggerNxExecuteFault((HO_VIRTUAL_ADDRESS)(uint64_t)&gPageFaultImportedTarget);
    HO_KPANIC(EC_INVALID_STATE, "Imported-region page-fault demo unexpectedly returned");
}

void
PageFaultGuardThread(void *arg)
{
    (void)arg;
    KTHREAD *thread = KeGetCurrentThread();
    if (!thread || thread->StackGuardBase == 0)
        HO_KPANIC(EC_INVALID_STATE, "Guard-page demo requires a KVA-managed thread stack");

    volatile uint64_t *guard = (volatile uint64_t *)(uint64_t)thread->StackGuardBase;
    klog(KLOG_LEVEL_INFO, "[PF-DEMO] triggering guard fault at %p\n", guard);
    volatile uint64_t value = *guard;
    (void)value;
    HO_KPANIC(EC_INVALID_STATE, "Guard-page demo unexpectedly returned");
}

void
PageFaultFixmapThread(void *arg)
{
    (void)arg;
    HO_PHYSICAL_ADDRESS physPage = 0;
    KE_TEMP_PHYS_MAP_HANDLE handle = {0};
    HO_VIRTUAL_ADDRESS fixmapVa = 0;

    HO_STATUS status = KePmmAllocPages(1, NULL, &physPage);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to allocate test page for fixmap page-fault demo");

    status = KeTempPhysMapAcquire(physPage, PTE_WRITABLE | PTE_GLOBAL | PTE_NO_EXECUTE, &handle, &fixmapVa);
    if (status != EC_SUCCESS)
    {
        (void)KePmmFreePages(physPage, 1);
        HO_KPANIC(status, "Failed to acquire fixmap alias for page-fault demo");
    }

    volatile uint64_t *alias = (volatile uint64_t *)(uint64_t)fixmapVa;
    *alias = 0x50464649584D4150ULL;

    TriggerNxExecuteFault(fixmapVa);

    status = KeTempPhysMapRelease(&handle);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Fixmap page-fault demo cleanup failed to release temp map");

    status = KePmmFreePages(physPage, 1);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Fixmap page-fault demo cleanup failed to free PMM page");
    HO_KPANIC(EC_INVALID_STATE, "Fixmap page-fault demo unexpectedly returned");
}

void
PageFaultHeapThread(void *arg)
{
    (void)arg;
    HO_VIRTUAL_ADDRESS heapVa = 0;

    HO_STATUS status = KeHeapAllocPages(1, &heapVa);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to allocate heap page for page-fault demo");

    volatile uint64_t *heapWords = (volatile uint64_t *)(uint64_t)heapVa;
    heapWords[0] = 0x5046484541505641ULL;

    TriggerNxExecuteFault(heapVa);

    status = KeHeapFreePages(heapVa);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Heap page-fault demo cleanup failed to free heap page");
    HO_KPANIC(EC_INVALID_STATE, "Heap page-fault demo unexpectedly returned");
}
