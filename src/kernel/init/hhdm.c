/**
 * HimuOperatingSystem
 *
 * File: init/hhdm.c
 * Description: HHDM and ACPI verification helpers for kernel bring-up.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "init_internal.h"

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

void
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
    BOOL allowZeroProbeFallback = !HoNullDetectionEnabled();

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

        if (candidateProbe == 0)
        {
            zeroProbeFound = TRUE;
            continue;
        }

        probePhys = candidateProbe;
        probeFound = TRUE;
        break;
    }

    if (!probeFound && zeroProbeFound && allowZeroProbeFallback)
    {
        probePhys = 0;
        probeFound = TRUE;
    }

    if (!probeFound)
    {
        if (HoNullDetectionEnabled())
        {
            if (zeroProbeFound)
                klog(KLOG_LEVEL_WARNING,
                     "[MM] FULL HHDM smoke test skipped: only page-0 probe candidates remain under NULL detection\n");
            else
                klog(KLOG_LEVEL_WARNING,
                     "[MM] FULL HHDM smoke test skipped: no reclaimable non-zero probe page\n");
        }
        else
        {
            klog(KLOG_LEVEL_WARNING, "[MM] FULL HHDM smoke test skipped: no reclaimable probe page\n");
        }
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

void
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
