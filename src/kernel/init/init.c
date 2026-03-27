#include "init_internal.h"
#include <kernel/init.h>
#include <drivers/serial.h>
#include <kernel/hodbg.h>
#include <arch/amd64/idt.h> // TODO: remove dependency on x86 arch
#include <arch/amd64/acpi.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/time_source.h>
#include <kernel/ke/clock_event.h>
#include <kernel/ke/pool.h>
#include <kernel/ke/kthread.h>
#include <kernel/ke/scheduler.h>
#include "assets/fonts/font8x16.h"

//
// Global kernel variables that need to be initialized at startup
//

KE_VIDEO_DRIVER gVideoDriver;
ARCH_BASIC_CPU_INFO gBasicCpuInfo;
BITMAP_FONT_INFO gSystemFont;
static BOOT_CAPSULE *gBootCapsule;

static void InitBitmapFont(void);
static void InitCpuState(STAGING_BLOCK *block);
static void AssertRsdp(HO_VIRTUAL_ADDRESS rsdpVirt);

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
    VerifyHhdm(block);
    initStatus = ImportBootMappings(block);
    if (initStatus != EC_SUCCESS)
    {
        HO_KPANIC(initStatus, "Failed to import boot mapping manifest");
    }
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

    // Smoke test: single page alloc/write/read/free
    {
        HO_PHYSICAL_ADDRESS testPage;
        initStatus = KePmmAllocPages(1, (void *)0, &testPage);
        if (initStatus == EC_SUCCESS)
        {
            volatile uint64_t *va = (volatile uint64_t *)HHDM_PHYS2VIRT(testPage);
            *va = 0xDEADBEEFCAFEBABEULL;
            HO_KASSERT(*va == 0xDEADBEEFCAFEBABEULL, EC_INVALID_STATE);
            KePmmFreePages(testPage, 1);
            klog(KLOG_LEVEL_INFO, "[PMM] smoke: 1-page alloc/write/read/free OK\n");
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

static void
InitBitmapFont(void)
{
    gSystemFont.Width = FONT_WIDTH;
    gSystemFont.Height = FONT_HEIGHT;
    gSystemFont.GlyphCount = FONT_GLYPH_COUNT;
    gSystemFont.RawGlyphs = (const uint8_t *)gFont8x16Data;
    gSystemFont.RowStride = 1u;
    gSystemFont.GlyphStride = 16u;
}

static void
InitCpuState(STAGING_BLOCK *block)
{
    CPU_CORE_LOCAL_DATA *data = &block->CpuInfo;
    data->Tss.RSP0 = HHDM_PHYS2VIRT(block->KrnlStackPhys) + block->Layout.KrnlStackSize;
    data->Tss.IST1 = HHDM_PHYS2VIRT(block->KrnlIST1StackPhys) + block->Layout.IST1StackSize;
    data->Tss.IOMapBase = sizeof(TSS64); // No IO permission bitmap
}

static void
AssertRsdp(HO_VIRTUAL_ADDRESS rsdpVirt)
{
    ACPI_RSDP *rsdp = (void *)rsdpVirt;
    if (rsdp->Signature[0] != 'R' || rsdp->Signature[1] != 'S' || rsdp->Signature[2] != 'D' ||
        rsdp->Signature[3] != ' ' || rsdp->Signature[4] != 'P' || rsdp->Signature[5] != 'T' ||
        rsdp->Signature[6] != 'R')
    {
        HO_KPANIC(EC_NOT_SUPPORTED, "ACPI RSDP signature invalid");
    }

    if (rsdp->Revision < 2)
    {
        HO_KPANIC(EC_NOT_SUPPORTED, "ACPI Revision not supported (only v2.0+ supported)");
    }
}

HO_KERNEL_API BOOT_CAPSULE *
KeGetBootCapsule(void)
{
    return gBootCapsule;
}
