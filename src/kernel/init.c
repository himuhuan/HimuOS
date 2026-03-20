#include <kernel/init.h>
#include <drivers/serial.h>
#include <kernel/hodbg.h>
#include <arch/amd64/idt.h> // TODO: remove dependency on x86 arch
#include <arch/amd64/acpi.h>
#include <arch/amd64/efi_mem.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/time_source.h>
#include <kernel/ke/clock_event.h>
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
static void VerifyHhdm(STAGING_BLOCK *block);
static BOOL FindFirstSafeProbePage(UINT64 descStart,
                                   UINT64 descEndExclusive,
                                   UINT64 capsuleBase,
                                   UINT64 capsuleEndExclusive,
                                   UINT64 pageTableBase,
                                   UINT64 pageTableEndExclusive,
                                   UINT64 *probePhysOut);
static void DumpHhdmProbeDiagnostics(const EFI_MEMORY_MAP *map,
                                     UINT64 capsuleBase,
                                     UINT64 capsuleEndExclusive,
                                     UINT64 pageTableBase,
                                     UINT64 pageTableEndExclusive);
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
VerifyHhdm(STAGING_BLOCK *block)
{
    if (!block)
    {
        HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Boot capsule missing");
    }

    EFI_MEMORY_MAP *map = (EFI_MEMORY_MAP *)HHDM_PHYS2VIRT(block->MemoryMapPhys);
    if (!map || map->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) || map->DescriptorSize == 0)
    {
        HO_KPANIC(EC_INVALID_STATE, "Boot memory map unavailable for HHDM verification");
    }

    UINT64 capsuleBase = block->BasePhys;
    UINT64 capsuleEndExclusive = capsuleBase + (block->PageLayout.TotalPages << PAGE_SHIFT);
    UINT64 pageTableBase = block->PageTableInfo.Ptr;
    UINT64 pageTableEndExclusive = pageTableBase + block->PageTableInfo.Size;
    UINT64 descCount = map->DescriptorTotalSize / map->DescriptorSize;
    UINT64 probePhys = 0;
    BOOL probeFound = FALSE;
    BOOL zeroProbeFound = FALSE;

    for (UINT64 idx = 0; idx < descCount; ++idx)
    {
        UINT8 *descAddr = (UINT8 *)map->Segs + idx * map->DescriptorSize;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)descAddr;

        if (!desc || !IS_RECLAIMABLE_MEMORY(desc->Type) || desc->NumberOfPages == 0)
            continue;

        UINT64 descStart = desc->PhysicalStart;
        UINT64 descEndExclusive = descStart + (desc->NumberOfPages << PAGE_SHIFT);
        UINT64 candidateProbe = 0;
        if (!FindFirstSafeProbePage(descStart, descEndExclusive, capsuleBase, capsuleEndExclusive, pageTableBase,
                                    pageTableEndExclusive, &candidateProbe))
            continue;

        // Prefer a non-zero page when available; page 0 remains a fallback.
        if (candidateProbe == 0)
        {
            zeroProbeFound = TRUE;
            continue;
        }

        probePhys = candidateProbe;
        probeFound = TRUE;
        break;
    }

    if (!probeFound && zeroProbeFound)
    {
        probePhys = 0;
        probeFound = TRUE;
    }

    if (!probeFound)
    {
        klog(KLOG_LEVEL_WARNING, "[MM] FULL HHDM smoke test skipped: no reclaimable probe page\n");
        DumpHhdmProbeDiagnostics(map, capsuleBase, capsuleEndExclusive, pageTableBase, pageTableEndExclusive);
        return;
    }

    volatile UINT64 *probeVirt = (volatile UINT64 *)HHDM_PHYS2VIRT(probePhys);
    UINT64 oldValue = *probeVirt;
    UINT64 pattern = 0x4848444D534D4F4BULL ^ probePhys;

    *probeVirt = pattern;
    UINT64 readBack = *probeVirt;
    *probeVirt = oldValue;

    if (readBack != pattern)
    {
        HO_KPANIC(EC_INVALID_STATE, "HHDM verification failed");
    }

    klog(KLOG_LEVEL_INFO, "[MM] FULL HHDM smoke test OK: PA=%p VA=%p\n", (void *)(UINTN)probePhys,
         (void *)(UINTN)HHDM_PHYS2VIRT(probePhys));
}

static BOOL
FindFirstSafeProbePage(UINT64 descStart,
                       UINT64 descEndExclusive,
                       UINT64 capsuleBase,
                       UINT64 capsuleEndExclusive,
                       UINT64 pageTableBase,
                       UINT64 pageTableEndExclusive,
                       UINT64 *probePhysOut)
{
    typedef struct HHDM_EXCLUDE_RANGE
    {
        UINT64 Start;
        UINT64 EndExclusive;
    } HHDM_EXCLUDE_RANGE;

    HHDM_EXCLUDE_RANGE ranges[2] = {
        {capsuleBase, capsuleEndExclusive},
        {pageTableBase, pageTableEndExclusive},
    };

    if (ranges[1].Start < ranges[0].Start)
    {
        HHDM_EXCLUDE_RANGE tmp = ranges[0];
        ranges[0] = ranges[1];
        ranges[1] = tmp;
    }

    if (probePhysOut)
        *probePhysOut = 0;

    UINT64 candidate = descStart;
    for (UINT64 idx = 0; idx < 2; ++idx)
    {
        UINT64 rangeStart = ranges[idx].Start;
        UINT64 rangeEndExclusive = ranges[idx].EndExclusive;
        if (rangeStart >= rangeEndExclusive || rangeEndExclusive <= descStart || rangeStart >= descEndExclusive)
            continue;

        if (candidate < rangeStart)
        {
            if (probePhysOut)
                *probePhysOut = candidate;
            return TRUE;
        }

        if (candidate < rangeEndExclusive)
            candidate = rangeEndExclusive;

        if (candidate >= descEndExclusive)
            return FALSE;
    }

    if (candidate >= descEndExclusive)
        return FALSE;

    if (probePhysOut)
        *probePhysOut = candidate;
    return TRUE;
}

static void
DumpHhdmProbeDiagnostics(const EFI_MEMORY_MAP *map,
                         UINT64 capsuleBase,
                         UINT64 capsuleEndExclusive,
                         UINT64 pageTableBase,
                         UINT64 pageTableEndExclusive)
{
    if (!map || map->DescriptorSize < sizeof(EFI_MEMORY_DESCRIPTOR) || map->DescriptorSize == 0)
        return;

    UINT64 descCount = map->DescriptorTotalSize / map->DescriptorSize;
    klog(KLOG_LEVEL_INFO,
         "[MM] probe filter ranges: capsule=[%p,%p) page_tables=[%p,%p) recorded_pt_bytes=%u desc_count=%u\n",
         (void *)(UINTN)capsuleBase, (void *)(UINTN)capsuleEndExclusive, (void *)(UINTN)pageTableBase,
         (void *)(UINTN)pageTableEndExclusive, pageTableEndExclusive - pageTableBase, descCount);

    for (UINT64 idx = 0; idx < descCount; ++idx)
    {
        UINT8 *descAddr = (UINT8 *)map->Segs + idx * map->DescriptorSize;
        EFI_MEMORY_DESCRIPTOR *desc = (EFI_MEMORY_DESCRIPTOR *)descAddr;
        if (!desc || !IS_RECLAIMABLE_MEMORY(desc->Type) || desc->NumberOfPages == 0)
            continue;

        UINT64 descStart = desc->PhysicalStart;
        UINT64 descEndExclusive = descStart + (desc->NumberOfPages << PAGE_SHIFT);
        BOOL overlapsCapsule = !(descEndExclusive <= capsuleBase || descStart >= capsuleEndExclusive);
        BOOL overlapsPageTables = !(descEndExclusive <= pageTableBase || descStart >= pageTableEndExclusive);
        UINT64 firstSafeProbe = 0;
        BOOL hasSafeProbe = FindFirstSafeProbePage(descStart, descEndExclusive, capsuleBase, capsuleEndExclusive,
                                                   pageTableBase, pageTableEndExclusive, &firstSafeProbe);

        klog(KLOG_LEVEL_INFO,
             "[MM] reclaimable desc[%u]: type=%u phys=[%p,%p) pages=%u overlap(capsule=%u,pt=%u) first_safe=%p\n", idx,
             desc->Type, (void *)(UINTN)descStart, (void *)(UINTN)descEndExclusive, desc->NumberOfPages,
             overlapsCapsule, overlapsPageTables, (void *)(UINTN)(hasSafeProbe ? firstSafeProbe : 0));
    }
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
