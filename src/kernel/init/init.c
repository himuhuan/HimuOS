#include "init_internal.h"
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

    HO_STATUS status = KiQueryPhysicalStats(&basePhysical);
    if (status != EC_SUCCESS)
        return status;
    status = KiQueryVmmOverview(&baseVmm);
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

    status = KeTempPhysMapRelease(&tempMap);
    if (status != EC_SUCCESS)
        goto cleanup;
    tempMapped = FALSE;

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

    if (heapPhysical.AllocatedBytes < basePhysical.AllocatedBytes + PAGE_4KB || heapPhysical.FreeBytes > basePhysical.FreeBytes)
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

    if (finalPhysical.TotalBytes != basePhysical.TotalBytes || finalPhysical.FreeBytes != basePhysical.FreeBytes ||
        finalPhysical.AllocatedBytes != basePhysical.AllocatedBytes || finalPhysical.ReservedBytes != basePhysical.ReservedBytes)
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

    klog(KLOG_LEVEL_INFO,
         "[OBS] memory observability self-test OK: pmm/vmm counters tracked fixmap+heap activity and recovered\n");
    return EC_SUCCESS;

cleanup:
    if (tempMapped)
    {
        HO_STATUS cleanupStatus = KeTempPhysMapRelease(&tempMap);
        if (status == EC_SUCCESS)
            status = cleanupStatus;
    }

    if (hasTempPhys)
    {
        HO_STATUS cleanupStatus = KePmmFreePages(tempPhys, 1);
        if (status == EC_SUCCESS)
            status = cleanupStatus;
    }

    if (heapAllocated)
    {
        HO_STATUS cleanupStatus = KeHeapFreePages(heapVirt);
        if (status == EC_SUCCESS)
            status = cleanupStatus;
    }

    return status;
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

    // Smoke test: single page alloc/write/read/free
    {
        HO_PHYSICAL_ADDRESS testPage;
        initStatus = KePmmAllocPages(1, (void *)0, &testPage);
        if (initStatus == EC_SUCCESS)
        {
            // Runtime RAM smoke test should use a temporary fixmap alias, not a persistent HHDM ownership address.
            KE_TEMP_PHYS_MAP_HANDLE tempMap = {0};
            HO_VIRTUAL_ADDRESS tempVirt = 0;
            initStatus = KeTempPhysMapAcquire(testPage, PTE_WRITABLE | PTE_GLOBAL | PTE_NO_EXECUTE, &tempMap, &tempVirt);
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
                (void)KePmmFreePages(testPage, 1);
                HO_KPANIC(initStatus, "Failed to release temporary mapping for PMM smoke test");
            }

            KePmmFreePages(testPage, 1);
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

    // ---- KTHREAD Object Pool ----
    // Depends on KeKvaInit(): KeKThreadPoolInit() -> KePoolInit() ->
    // KeHeapAllocPages().
    initStatus = KeKThreadPoolInit();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize KTHREAD pool");
    }

    // Pool smoke test: alloc/free/realloc
    {
        KE_POOL testPool;
        initStatus = KePoolInit(&testPool, 64, 8, "smoke-test");
        if (initStatus == EC_SUCCESS)
        {
            void *a = KePoolAlloc(&testPool);
            void *b = KePoolAlloc(&testPool);
            HO_KASSERT(a != NULL && b != NULL && a != b, EC_INVALID_STATE);
            KePoolFree(&testPool, a);
            void *c = KePoolAlloc(&testPool);
            HO_KASSERT(c == a, EC_INVALID_STATE);
            KePoolFree(&testPool, b);
            KePoolFree(&testPool, c);
            klog(KLOG_LEVEL_INFO, "[POOL] smoke: alloc/free/realloc OK\n");
        }
    }

    // ---- Scheduler ----
    initStatus = KeSchedulerInit();
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to initialize scheduler");
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
