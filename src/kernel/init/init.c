#include "init_internal.h"

#include <kernel/ex/ex_bootstrap.h>
#include <kernel/ke/sysinfo.h>

//
// Global kernel variables that need to be initialized at startup
//

KE_VIDEO_DRIVER gVideoDriver;
ARCH_BASIC_CPU_INFO gBasicCpuInfo;
BITMAP_FONT_INFO gSystemFont;
static BOOT_CAPSULE *gBootCapsule;
static const BOOT_MAPPING_MANIFEST_HEADER *gBootMappingManifest;

static HO_STATUS
KiQueryPhysicalStats(SYSINFO_PHYSICAL_MEM_STATS *outStats)
{
    if (!outStats)
        return EC_ILLEGAL_ARGUMENT;
    return KeQuerySystemInformation(KE_SYSINFO_PHYSICAL_MEM_STATS, outStats, sizeof(*outStats), NULL);
}

static HO_STATUS
KiQueryVmmOverview(SYSINFO_VMM_OVERVIEW *outOverview)
{
    if (!outOverview)
        return EC_ILLEGAL_ARGUMENT;
    return KeQuerySystemInformation(KE_SYSINFO_VMM_OVERVIEW, outOverview, sizeof(*outOverview), NULL);
}

static HO_STATUS
KiQuerySchedulerInfo(KE_SYSINFO_SCHEDULER_DATA *outInfo)
{
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;
    return KeQuerySystemInformation(KE_SYSINFO_SCHEDULER, outInfo, sizeof(*outInfo), NULL);
}

static HO_STATUS
KiQueryClockEventInfo(SYSINFO_CLOCK_EVENT *outInfo)
{
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;
    return KeQuerySystemInformation(KE_SYSINFO_CLOCK_EVENT, outInfo, sizeof(*outInfo), NULL);
}

static HO_STATUS
KiQueryActiveKvaRanges(SYSINFO_ACTIVE_KVA_RANGES *outInfo)
{
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;
    return KeQuerySystemInformation(KE_SYSINFO_ACTIVE_KVA_RANGES, outInfo, sizeof(*outInfo), NULL);
}

static HO_STATUS
RunMemoryObservabilitySelfTest(void)
{
    SYSINFO_PHYSICAL_MEM_STATS basePhysical = {0};
    SYSINFO_PHYSICAL_MEM_STATS fixmapPhysical = {0};
    SYSINFO_PHYSICAL_MEM_STATS heapPhysical = {0};
    SYSINFO_PHYSICAL_MEM_STATS finalPhysical = {0};

    SYSINFO_VMM_OVERVIEW baseVmm = {0};
    SYSINFO_VMM_OVERVIEW fixmapVmm = {0};
    SYSINFO_VMM_OVERVIEW heapVmm = {0};
    SYSINFO_VMM_OVERVIEW finalVmm = {0};

    HO_PHYSICAL_ADDRESS tempPhys = 0;
    BOOL hasTempPhys = FALSE;
    KE_TEMP_PHYS_MAP_HANDLE tempMap = {0};
    BOOL tempMapped = FALSE;
    HO_VIRTUAL_ADDRESS tempVirt = 0;
    HO_VIRTUAL_ADDRESS heapVirt = 0;
    BOOL heapAllocated = FALSE;
    SYSINFO_ACTIVE_KVA_RANGES baseRanges = {0};
    SYSINFO_ACTIVE_KVA_RANGES fixmapRanges = {0};
    SYSINFO_ACTIVE_KVA_RANGES heapRanges = {0};
    SYSINFO_ACTIVE_KVA_RANGES finalRanges = {0};

    HO_STATUS status = KiQueryPhysicalStats(&basePhysical);
    if (status != EC_SUCCESS)
        return status;
    status = KiQueryVmmOverview(&baseVmm);
    if (status != EC_SUCCESS)
        return status;
    status = KiQueryActiveKvaRanges(&baseRanges);
    if (status != EC_SUCCESS)
        return status;

    status = KePmmAllocPages(1, NULL, &tempPhys);
    if (status != EC_SUCCESS)
        goto cleanup;
    hasTempPhys = TRUE;

    status = KeTempPhysMapAcquire(tempPhys, PTE_WRITABLE | PTE_GLOBAL | PTE_NO_EXECUTE, &tempMap, &tempVirt);
    if (status != EC_SUCCESS)
        goto cleanup;
    tempMapped = TRUE;

    volatile uint64_t *tempWords = (volatile uint64_t *)(uint64_t)tempVirt;
    tempWords[0] = 0x4F4253544D415031ULL;
    if (tempWords[0] != 0x4F4253544D415031ULL)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    status = KiQueryPhysicalStats(&fixmapPhysical);
    if (status != EC_SUCCESS)
        goto cleanup;
    status = KiQueryVmmOverview(&fixmapVmm);
    if (status != EC_SUCCESS)
        goto cleanup;
    status = KiQueryActiveKvaRanges(&fixmapRanges);
    if (status != EC_SUCCESS)
        goto cleanup;

    if (fixmapPhysical.AllocatedBytes < basePhysical.AllocatedBytes + PAGE_4KB ||
        fixmapPhysical.FreeBytes + PAGE_4KB > basePhysical.FreeBytes)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }
    if (fixmapVmm.FixmapActiveSlots != baseVmm.FixmapActiveSlots + 1 ||
        fixmapVmm.FixmapArena.ActiveAllocations != baseVmm.FixmapArena.ActiveAllocations + 1 ||
        fixmapVmm.ActiveKvaRangeCount < baseVmm.ActiveKvaRangeCount + 1)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }
    if (fixmapRanges.TotalActiveRangeCount < baseRanges.TotalActiveRangeCount + 1)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    BOOL foundFixmapRange = FALSE;
    for (uint32_t idx = 0; idx < fixmapRanges.ReturnedRangeCount; ++idx)
    {
        if (fixmapRanges.Ranges[idx].Arena == KE_KVA_ARENA_FIXMAP && fixmapRanges.Ranges[idx].UsableBase == tempVirt)
        {
            foundFixmapRange = TRUE;
            break;
        }
    }
    if (!foundFixmapRange && !fixmapRanges.Truncated)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    status = KeTempPhysMapRelease(&tempMap);
    if (status != EC_SUCCESS)
        goto cleanup;
    tempMapped = FALSE;
    tempVirt = 0;

    status = KePmmFreePages(tempPhys, 1);
    if (status != EC_SUCCESS)
        goto cleanup;
    hasTempPhys = FALSE;

    status = KeHeapAllocPages(1, &heapVirt);
    if (status != EC_SUCCESS)
        goto cleanup;
    heapAllocated = TRUE;

    volatile uint64_t *heapWords = (volatile uint64_t *)(uint64_t)heapVirt;
    heapWords[0] = 0x4F42534845415031ULL;
    if (heapWords[0] != 0x4F42534845415031ULL)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    status = KiQueryPhysicalStats(&heapPhysical);
    if (status != EC_SUCCESS)
        goto cleanup;
    status = KiQueryVmmOverview(&heapVmm);
    if (status != EC_SUCCESS)
        goto cleanup;
    status = KiQueryActiveKvaRanges(&heapRanges);
    if (status != EC_SUCCESS)
        goto cleanup;

    if (heapPhysical.AllocatedBytes < basePhysical.AllocatedBytes + PAGE_4KB ||
        heapPhysical.FreeBytes > basePhysical.FreeBytes)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }
    if (heapVmm.HeapArena.ActiveAllocations != baseVmm.HeapArena.ActiveAllocations + 1 ||
        heapVmm.ActiveKvaRangeCount < baseVmm.ActiveKvaRangeCount + 1 ||
        heapVmm.FixmapActiveSlots != baseVmm.FixmapActiveSlots)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }
    if (heapRanges.TotalActiveRangeCount < baseRanges.TotalActiveRangeCount + 1)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    BOOL foundHeapRange = FALSE;
    for (uint32_t idx = 0; idx < heapRanges.ReturnedRangeCount; ++idx)
    {
        if (heapRanges.Ranges[idx].Arena == KE_KVA_ARENA_HEAP && heapRanges.Ranges[idx].UsableBase == heapVirt)
        {
            foundHeapRange = TRUE;
            break;
        }
    }
    if (!foundHeapRange && !heapRanges.Truncated)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    status = KeHeapFreePages(heapVirt);
    if (status != EC_SUCCESS)
        goto cleanup;
    heapAllocated = FALSE;

    status = KiQueryPhysicalStats(&finalPhysical);
    if (status != EC_SUCCESS)
        goto cleanup;
    status = KiQueryVmmOverview(&finalVmm);
    if (status != EC_SUCCESS)
        goto cleanup;
    status = KiQueryActiveKvaRanges(&finalRanges);
    if (status != EC_SUCCESS)
        goto cleanup;

    if (finalPhysical.TotalBytes != basePhysical.TotalBytes || finalPhysical.FreeBytes != basePhysical.FreeBytes ||
        finalPhysical.AllocatedBytes != basePhysical.AllocatedBytes ||
        finalPhysical.ReservedBytes != basePhysical.ReservedBytes)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    if (finalVmm.ImportedRegionCount != baseVmm.ImportedRegionCount ||
        finalVmm.ActiveKvaRangeCount != baseVmm.ActiveKvaRangeCount ||
        finalVmm.FixmapTotalSlots != baseVmm.FixmapTotalSlots ||
        finalVmm.FixmapActiveSlots != baseVmm.FixmapActiveSlots ||
        finalVmm.StackArena.ActiveAllocations != baseVmm.StackArena.ActiveAllocations ||
        finalVmm.StackArena.FreePages != baseVmm.StackArena.FreePages ||
        finalVmm.FixmapArena.ActiveAllocations != baseVmm.FixmapArena.ActiveAllocations ||
        finalVmm.FixmapArena.FreePages != baseVmm.FixmapArena.FreePages ||
        finalVmm.HeapArena.ActiveAllocations != baseVmm.HeapArena.ActiveAllocations ||
        finalVmm.HeapArena.FreePages != baseVmm.HeapArena.FreePages)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }
    if (finalRanges.TotalActiveRangeCount != baseRanges.TotalActiveRangeCount ||
        finalRanges.ReturnedRangeCount != baseRanges.ReturnedRangeCount ||
        finalRanges.Truncated != baseRanges.Truncated)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    klog(KLOG_LEVEL_INFO, "[OBS] memory observability self-test OK: pmm/vmm counters and bounded KVA snapshots tracked "
                          "fixmap+heap activity\n");
    return EC_SUCCESS;

cleanup:
    if (tempMapped)
    {
        HO_STATUS cleanupStatus = KeTempPhysMapRelease(&tempMap);
        if (cleanupStatus == EC_SUCCESS)
        {
            tempMapped = FALSE;
            tempVirt = 0;
        }
        else
        {
            klog(KLOG_LEVEL_ERROR, "[OBS] cleanup preserved PMM page %p because temp-map release failed (%s, %p)\n",
                 (void *)(uint64_t)tempPhys, KrGetStatusMessage(cleanupStatus), cleanupStatus);
        }

        if (status == EC_SUCCESS)
            status = cleanupStatus;
    }

    if (hasTempPhys && !tempMapped)
    {
        HO_STATUS cleanupStatus = KePmmFreePages(tempPhys, 1);
        if (cleanupStatus == EC_SUCCESS)
        {
            hasTempPhys = FALSE;
        }
        else
        {
            klog(KLOG_LEVEL_ERROR, "[OBS] cleanup failed to free PMM page %p (%s, %p)\n", (void *)(uint64_t)tempPhys,
                 KrGetStatusMessage(cleanupStatus), cleanupStatus);
        }

        if (status == EC_SUCCESS)
            status = cleanupStatus;
    }
    else if (hasTempPhys)
    {
        klog(KLOG_LEVEL_ERROR, "[OBS] cleanup preserved PMM page %p because a temp alias is still active\n",
             (void *)(uint64_t)tempPhys);
    }

    if (heapAllocated)
    {
        HO_STATUS cleanupStatus = KeHeapFreePages(heapVirt);
        if (status == EC_SUCCESS)
            status = cleanupStatus;
    }

    return status;
}

static HO_STATUS
RunClockEventObservabilitySelfTest(void)
{
    SYSINFO_CLOCK_EVENT info = {0};
    HO_STATUS status = KiQueryClockEventInfo(&info);
    if (status != EC_SUCCESS)
        return status;

    if (!info.Ready || info.FreqHz == 0 || info.VectorNumber == 0 || info.SourceName[0] == '\0')
        return EC_INVALID_STATE;

    if (info.MinDeltaNs == 0 || info.MaxDeltaNs == 0 || info.MinDeltaNs > info.MaxDeltaNs)
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

static HO_STATUS
RunAllocatorObservabilitySelfTest(void)
{
    KE_ALLOCATOR_STATS baseStats = {0};
    KE_ALLOCATOR_STATS liveStats = {0};
    KE_ALLOCATOR_STATS finalStats = {0};

    void *smallAlloc = NULL;
    void *zeroAlloc = NULL;
    void *largeAlloc = NULL;
    BOOL hasSmall = FALSE;
    BOOL hasZero = FALSE;
    BOOL hasLarge = FALSE;

    HO_STATUS status = KeAllocatorQueryStats(&baseStats);
    if (status != EC_SUCCESS)
        return status;

    smallAlloc = kmalloc(24);
    if (!smallAlloc)
    {
        status = EC_OUT_OF_RESOURCE;
        goto cleanup;
    }
    hasSmall = TRUE;

    zeroAlloc = kzalloc(64);
    if (!zeroAlloc)
    {
        status = EC_OUT_OF_RESOURCE;
        goto cleanup;
    }
    hasZero = TRUE;

    largeAlloc = kmalloc(5000);
    if (!largeAlloc)
    {
        status = EC_OUT_OF_RESOURCE;
        goto cleanup;
    }
    hasLarge = TRUE;

    for (uint32_t i = 0; i < 64; ++i)
    {
        if (((uint8_t *)zeroAlloc)[i] != 0)
        {
            status = EC_INVALID_STATE;
            goto cleanup;
        }
    }

    ((uint8_t *)smallAlloc)[0] = 0x5A;
    ((uint8_t *)largeAlloc)[0] = 0xA5;
    ((uint8_t *)largeAlloc)[4096] = 0x11;
    ((uint8_t *)largeAlloc)[4999] = 0x22;

    status = KeAllocatorQueryStats(&liveStats);
    if (status != EC_SUCCESS)
        goto cleanup;

    if (liveStats.LiveAllocationCount != baseStats.LiveAllocationCount + 3 ||
        liveStats.LiveSmallAllocationCount != baseStats.LiveSmallAllocationCount + 2 ||
        liveStats.LiveLargeAllocationCount != baseStats.LiveLargeAllocationCount + 1 ||
        liveStats.BackingBytes < baseStats.BackingBytes + PAGE_4KB ||
        liveStats.FailedAllocationCount != baseStats.FailedAllocationCount)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    KE_VA_DIAGNOSIS smallDiagnosis = {0};
    status = KeDiagnoseVirtualAddress(NULL, (HO_VIRTUAL_ADDRESS)(uint64_t)smallAlloc + 8, &smallDiagnosis);
    if (status != EC_SUCCESS)
        goto cleanup;
    if (smallDiagnosis.KvaStatus != EC_SUCCESS || smallDiagnosis.KvaInfo.Kind != KE_KVA_ADDRESS_ACTIVE_HEAP ||
        smallDiagnosis.AllocatorStatus != EC_SUCCESS || !smallDiagnosis.AllocatorInfo.LiveAllocation ||
        smallDiagnosis.AllocatorInfo.Kind != KE_ALLOCATOR_ALLOCATION_SMALL ||
        smallDiagnosis.AllocatorInfo.AllocationBase != (HO_VIRTUAL_ADDRESS)(uint64_t)smallAlloc)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    KE_VA_DIAGNOSIS largeDiagnosis = {0};
    status = KeDiagnoseVirtualAddress(NULL, (HO_VIRTUAL_ADDRESS)(uint64_t)largeAlloc + 128, &largeDiagnosis);
    if (status != EC_SUCCESS)
        goto cleanup;
    if (largeDiagnosis.KvaStatus != EC_SUCCESS || largeDiagnosis.KvaInfo.Kind != KE_KVA_ADDRESS_ACTIVE_HEAP ||
        largeDiagnosis.AllocatorStatus != EC_SUCCESS || !largeDiagnosis.AllocatorInfo.LiveAllocation ||
        largeDiagnosis.AllocatorInfo.Kind != KE_ALLOCATOR_ALLOCATION_LARGE ||
        largeDiagnosis.AllocatorInfo.AllocationBase != (HO_VIRTUAL_ADDRESS)(uint64_t)largeAlloc ||
        largeDiagnosis.AllocatorInfo.RequestedSize < 5000)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

cleanup:
    if (hasLarge)
        kfree(largeAlloc);
    if (hasZero)
        kfree(zeroAlloc);
    if (hasSmall)
        kfree(smallAlloc);

    if (status != EC_SUCCESS)
        return status;

    status = KeAllocatorQueryStats(&finalStats);
    if (status != EC_SUCCESS)
        return status;

    if (finalStats.LiveAllocationCount != baseStats.LiveAllocationCount ||
        finalStats.LiveSmallAllocationCount != baseStats.LiveSmallAllocationCount ||
        finalStats.LiveLargeAllocationCount != baseStats.LiveLargeAllocationCount ||
        finalStats.BackingBytes != baseStats.BackingBytes ||
        finalStats.FailedAllocationCount != baseStats.FailedAllocationCount)
    {
        return EC_INVALID_STATE;
    }

    klog(KLOG_LEVEL_INFO,
         "[OBS] allocator observability self-test OK: small/large/stats/diagnosis contracts verified\n");
    return EC_SUCCESS;
}

static HO_STATUS
RunSchedulerObservabilitySelfTest(void)
{
    KE_SYSINFO_SCHEDULER_DATA info = {0};
    HO_STATUS status = KiQuerySchedulerInfo(&info);
    if (status != EC_SUCCESS)
        return status;

    if (!info.SchedulerEnabled || info.CurrentThreadId != 0 || info.IdleThreadId != 0 || info.ReadyQueueDepth != 0 ||
        info.SleepQueueDepth != 0 || info.EarliestWakeDeadline != 0 || info.NextProgrammedDeadline != 0 ||
        info.ActiveThreadCount != 1 || info.TotalThreadsCreated != 1)
    {
        return EC_INVALID_STATE;
    }

    return EC_SUCCESS;
}

void
InitKernel(MAYBE_UNUSED STAGING_BLOCK *block)
{
    gBootCapsule = block;
    InitCpuState(block);
    InitBitmapFont();
    VdInit(&gVideoDriver, block);
    VdClearScreen(&gVideoDriver, COLOR_BLACK);
    ConsoleInit(&gVideoDriver, &gSystemFont);

    HO_STATUS initStatus;
    initStatus = IdtInit();
    if (initStatus != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR, "FATAL: HimuOS initialization failed!\n");
        while (1)
            ;
    }
    gBootMappingManifest = ValidateBootMappingManifest(block);
    VerifyHhdm(block);
    AssertRsdp(HHDM_PHYS2VIRT(block->AcpiRsdpPhys));
    GetBasicCpuInfo(&gBasicCpuInfo);

    // ---- Physical Memory Manager ----
    initStatus = KePmmInitFromBootMemoryMap(block);
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize PMM");
    }

    // PMM summary
    KE_PMM_STATS pmmStats;
    if (KePmmQueryStats(&pmmStats) == EC_SUCCESS)
    {
        klog(KLOG_LEVEL_INFO, "[PMM] total=%luKB free=%luKB reserved=%luKB\n", pmmStats.TotalBytes / 1024,
             pmmStats.FreeBytes / 1024, pmmStats.ReservedBytes / 1024);
    }

    initStatus = KeImportKernelAddressSpace(block, gBootMappingManifest);
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to import kernel address space");
    }

    initStatus = KePtSelfTest();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "PT HAL self-test failed");
    }

    // The KVA heap foundation must exist before pool-backed subsystems (for
    // example the KTHREAD pool) can initialize, because pool growth now uses
    // KeHeapAllocPages() instead of direct PMM allocation.
    initStatus = KeKvaInit();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize KVA");
    }

    initStatus = KeKvaSelfTest();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "KVA self-test failed");
    }

    initStatus = RunMemoryObservabilitySelfTest();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Memory observability self-test failed");
    }

    // Bring up allocator layer after KVA+observability are ready and before
    // pool-backed subsystems, while keeping KePool's existing semantics.
    initStatus = KeAllocatorInit();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize allocator layer");
    }

    initStatus = RunAllocatorObservabilitySelfTest();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Allocator observability self-test failed");
    }

    initStatus = ConsolePromoteAllocatorStorage();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to promote console mux storage onto allocator layer");
    }

    // Smoke test: single page alloc/write/read/free
    {
        HO_PHYSICAL_ADDRESS testPage;
        initStatus = KePmmAllocPages(1, (void *)0, &testPage);
        if (initStatus == EC_SUCCESS)
        {
            // Runtime RAM smoke test should use a temporary fixmap alias, not a persistent HHDM ownership address.
            KE_TEMP_PHYS_MAP_HANDLE tempMap = {0};
            HO_VIRTUAL_ADDRESS tempVirt = 0;
            initStatus =
                KeTempPhysMapAcquire(testPage, PTE_WRITABLE | PTE_GLOBAL | PTE_NO_EXECUTE, &tempMap, &tempVirt);
            if (initStatus != EC_SUCCESS)
            {
                (void)KePmmFreePages(testPage, 1);
                HO_KPANIC(initStatus, "Failed to acquire temporary mapping for PMM smoke test");
            }

            volatile uint64_t *va = (volatile uint64_t *)(uint64_t)tempVirt;
            *va = 0xDEADBEEFCAFEBABEULL;
            HO_KASSERT(*va == 0xDEADBEEFCAFEBABEULL, EC_INVALID_STATE);

            initStatus = KeTempPhysMapRelease(&tempMap);
            if (initStatus != EC_SUCCESS)
            {
                HO_KPANIC(initStatus, "Failed to release temporary mapping for PMM smoke test; backing page preserved");
            }
            tempVirt = 0;

            initStatus = KePmmFreePages(testPage, 1);
            if (initStatus != EC_SUCCESS)
                HO_KPANIC(initStatus, "Failed to free PMM page for PMM smoke test");
            klog(KLOG_LEVEL_INFO, "[PMM] smoke: 1-page alloc/temp-map/write/read/free OK\n");
        }

        // 4 contiguous pages
        HO_PHYSICAL_ADDRESS testPages;
        initStatus = KePmmAllocPages(4, (void *)0, &testPages);
        if (initStatus == EC_SUCCESS)
        {
            HO_KASSERT(HO_IS_ALIGNED(testPages, PAGE_4KB), EC_INVALID_STATE);
            KePmmFreePages(testPages, 4);
            klog(KLOG_LEVEL_INFO, "[PMM] smoke: 4-page contiguous alloc/free OK\n");
        }
    }

    initStatus = KeTimeSourceInit(block->AcpiRsdpPhys);
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize time source");
    }

    initStatus = KeClockEventInit();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize clock event");
    }

    initStatus = RunClockEventObservabilitySelfTest();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Clock-event observability self-test failed");
    }

    // ---- KTHREAD Object Pool ----
    // Depends on KeKvaInit(): KeKThreadPoolInit() -> KePoolInit() ->
    // KeHeapAllocPages().
    initStatus = KeKThreadPoolInit();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize KTHREAD pool");
    }

    // Pool smoke test: alloc/free/realloc + destroy + re-init
    {
        KE_POOL testPool;
        initStatus = KePoolInit(&testPool, 64, 8, "smoke-test");
        if (initStatus == EC_SUCCESS)
        {
            void *a = KePoolAlloc(&testPool);
            void *b = KePoolAlloc(&testPool);
            HO_KASSERT(a != NULL && b != NULL && a != b, EC_INVALID_STATE);

            // Destroy with live objects must fail
            HO_KASSERT(KePoolDestroy(&testPool) != EC_SUCCESS, EC_INVALID_STATE);

            KePoolFree(&testPool, a);
            void *c = KePoolAlloc(&testPool);
            HO_KASSERT(c == a, EC_INVALID_STATE);
            KePoolFree(&testPool, b);
            KePoolFree(&testPool, c);

            // Verify stats before destroy
            KE_POOL_STATS stats;
            KePoolQueryStats(&testPool, &stats);
            HO_KASSERT(stats.UsedSlots == 0, EC_INVALID_STATE);
            HO_KASSERT(stats.PeakUsedSlots == 2, EC_INVALID_STATE);
            HO_KASSERT(stats.PageCount >= 1, EC_INVALID_STATE);

            // Destroy with all objects returned must succeed
            initStatus = KePoolDestroy(&testPool);
            HO_KASSERT(initStatus == EC_SUCCESS, EC_INVALID_STATE);

            // Alloc on destroyed pool must return NULL
            HO_KASSERT(KePoolAlloc(&testPool) == NULL, EC_INVALID_STATE);

            // Re-init after destroy must succeed
            initStatus = KePoolInit(&testPool, 64, 8, "smoke-reinit");
            HO_KASSERT(initStatus == EC_SUCCESS, EC_INVALID_STATE);
            void *d = KePoolAlloc(&testPool);
            HO_KASSERT(d != NULL, EC_INVALID_STATE);
            KePoolFree(&testPool, d);
            initStatus = KePoolDestroy(&testPool);
            HO_KASSERT(initStatus == EC_SUCCESS, EC_INVALID_STATE);

            klog(KLOG_LEVEL_INFO, "[POOL] smoke: alloc/free/realloc/destroy/re-init OK\n");
        }
    }

    // ---- Scheduler ----
    initStatus = KeSchedulerInit();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize scheduler");
    }

    initStatus = RunSchedulerObservabilitySelfTest();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Scheduler observability self-test failed");
    }

    initStatus = ExBootstrapInit();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize Ex bootstrap runtime");
    }
}

HO_KERNEL_API BOOT_CAPSULE *
KeGetBootCapsule(void)
{
    return gBootCapsule;
}

HO_KERNEL_API const BOOT_MAPPING_MANIFEST_HEADER *
KeGetBootMappingManifest(void)
{
    return gBootMappingManifest;
}
