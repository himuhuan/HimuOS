/**
 * HimuOperatingSystem
 *
 * File: ke/pmm/pmm_boot_init.c
 * Description:
 * Ke Layer - PMM initialization from EFI boot memory map
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "bitmap_sink.h"
#include "pmm_device.h"

#include <arch/amd64/efi_mem.h>
#include <boot/boot_capsule.h>
#include <kernel/hodbg.h>
#include <kernel/ke/mm.h>

#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#define PMM_LOW_RESERVED_BYTES (1ULL << 20)
#define PMM_LOW_RESERVED_PAGES (PMM_LOW_RESERVED_BYTES >> PAGE_SHIFT)

// External: the global PMM device defined in pmm_device.c
extern KE_PMM_DEVICE gPmmDevice;
extern KE_PMM_BITMAP_CONTEXT gBitmapCtx;

// ============================================================================
// 2-bit bitmap helpers (duplicated from bitmap_sink.c to avoid cross-TU coupling)
// ============================================================================

static inline KE_PMM_PAGE_STATE
BitmapGetState(const KE_PMM_BITMAP_CONTEXT *ctx, uint64_t pageIndex)
{
    uint64_t byteIdx = pageIndex / 4;
    uint64_t bitOff = (pageIndex % 4) * 2;
    return (KE_PMM_PAGE_STATE)((ctx->Bitmap[byteIdx] >> bitOff) & 0x3);
}

static inline void
BitmapSetState(KE_PMM_BITMAP_CONTEXT *ctx, uint64_t pageIndex, KE_PMM_PAGE_STATE state)
{
    uint64_t byteIdx = pageIndex / 4;
    uint64_t bitOff = (pageIndex % 4) * 2;
    ctx->Bitmap[byteIdx] = (uint8_t)((ctx->Bitmap[byteIdx] & ~(0x3 << bitOff)) | ((uint8_t)state << bitOff));
}

typedef struct _BOOT_RESERVED_RANGE
{
    HO_PHYSICAL_ADDRESS Start;
    uint64_t Pages;
} BOOT_RESERVED_RANGE;

static inline BOOL
PhysRangesOverlap(HO_PHYSICAL_ADDRESS aStart,
                  HO_PHYSICAL_ADDRESS aEnd,
                  HO_PHYSICAL_ADDRESS bStart,
                  HO_PHYSICAL_ADDRESS bEnd)
{
    return (aStart < bEnd) && (bStart < aEnd);
}

// Insertion sort helper
static void
SortDescriptorsByPhysAddr(EFI_MEMORY_DESCRIPTOR *descs, size_t count, size_t descSize)
{
    for (size_t i = 1; i < count; i++)
    {
        uint8_t *current = (uint8_t *)descs + i * descSize;
        EFI_MEMORY_DESCRIPTOR tmp;
        memcpy(&tmp, current, sizeof(EFI_MEMORY_DESCRIPTOR));

        size_t j = i;
        while (j > 0)
        {
            uint8_t *prev = (uint8_t *)descs + (j - 1) * descSize;
            EFI_MEMORY_DESCRIPTOR *prevDesc = (EFI_MEMORY_DESCRIPTOR *)prev;
            if (prevDesc->PhysicalStart <= tmp.PhysicalStart)
                break;
            memcpy((uint8_t *)descs + j * descSize, prev, sizeof(EFI_MEMORY_DESCRIPTOR));
            j--;
        }
        memcpy((uint8_t *)descs + j * descSize, &tmp, sizeof(EFI_MEMORY_DESCRIPTOR));
    }
}

// Reserve a physical range within the managed set (silently skips non-overlapping parts)
static void
ReserveBootRange(KE_PMM_BITMAP_CONTEXT *ctx, HO_PHYSICAL_ADDRESS rangeStart, uint64_t rangePages)
{
    if (rangePages == 0)
        return;

    HO_PHYSICAL_ADDRESS rangeEnd = rangeStart + rangePages * PAGE_4KB;
    HO_PHYSICAL_ADDRESS managedEnd = ctx->ManagedBasePhys + ctx->TotalManagedPages * PAGE_4KB;

    // Clip to managed range
    HO_PHYSICAL_ADDRESS clipStart = rangeStart < ctx->ManagedBasePhys ? ctx->ManagedBasePhys : rangeStart;
    HO_PHYSICAL_ADDRESS clipEnd = rangeEnd > managedEnd ? managedEnd : rangeEnd;

    if (clipStart >= clipEnd)
        return;

    uint64_t startIdx = (clipStart - ctx->ManagedBasePhys) >> PAGE_SHIFT;
    uint64_t pageCount = (clipEnd - clipStart) >> PAGE_SHIFT;

    for (uint64_t i = 0; i < pageCount; i++)
    {
        uint64_t idx = startIdx + i;
        KE_PMM_PAGE_STATE s = BitmapGetState(ctx, idx);
        if (s == PMM_PAGE_FREE)
        {
            BitmapSetState(ctx, idx, PMM_PAGE_RESERVED);
            ctx->FreePages--;
            ctx->ReservedPages++;
        }
        // Already reserved or allocated pages are left as-is (no error)
    }
}

// ============================================================================
// PMM initialization
//
// FAST-FAIL:
// If the UEFI memory descriptors passed from the bootloader to the kernel are
// not even 4KB aligned, it indicates that the underlying firmware (BIOS/UEFI)
// has an extremely serious flaw, or the boot data structures in memory
// have already been corrupted.
// ============================================================================
HO_KERNEL_API HO_NODISCARD HO_STATUS
KePmmInitFromBootMemoryMap(BOOT_CAPSULE *capsule)
{
    if (!capsule)
        return EC_ILLEGAL_ARGUMENT;

    EFI_MEMORY_MAP *memMap = (EFI_MEMORY_MAP *)HHDM_PHYS2VIRT(capsule->MemoryMapPhys);
    if (!memMap || memMap->DescriptorCount == 0)
        return EC_ILLEGAL_ARGUMENT;

    size_t descCount = memMap->DescriptorCount;
    size_t descSize = memMap->DescriptorSize;
    EFI_MEMORY_DESCRIPTOR *descs = memMap->Segs;

    // ---- Step 1: Sort descriptors by physical address ----
    SortDescriptorsByPhysAddr(descs, descCount, descSize);

    // ---- Step 2: Validate & identify managed range ----
    HO_PHYSICAL_ADDRESS managedLowest = UINT64_MAX;
    HO_PHYSICAL_ADDRESS managedHighest = 0;
    uint64_t totalReclaimablePages = 0;

    for (size_t i = 0; i < descCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)descs + i * descSize);

        if (d->NumberOfPages == 0)
            continue;

        HO_PHYSICAL_ADDRESS start = d->PhysicalStart;
        uint64_t npages = d->NumberOfPages;

        // Trim non-page-aligned boundaries inward --- useless
        // The subsequent code reused the original d->PhysicalStart and d->NumberOfPages,
        // without applying the previous 'trim inward'. However, considering:
        // 1. If defensive logic is written in the front, complex misalignment situations must be considered later
        // (obviously, they were not considered later).
        // 2. In reality, it usually doesn't cause problems, because UEFI should already be page-aligned.
        if (!HO_IS_ALIGNED(start, PAGE_4KB))
        {
#if 0
            HO_PHYSICAL_ADDRESS aligned = HO_ALIGN_UP(start, PAGE_4KB);
            uint64_t trimBytes = aligned - start;
            uint64_t trimPages = (trimBytes + PAGE_4KB - 1) / PAGE_4KB;
            if (trimPages >= npages)
                continue;
            start = aligned;
            npages -= trimPages;
#endif
            return EC_NOT_SUPPORTED;
        }

        HO_PHYSICAL_ADDRESS end = start + npages * PAGE_4KB;

        // Wraparound check
        if (end < start)
        {
            klog(KLOG_LEVEL_ERROR, "PMM: descriptor %lu wraps around (start=0x%lx, pages=%lu)\n", i, start, npages);
            return EC_ILLEGAL_ARGUMENT;
        }

        // Overlap check with previous descriptor
        if (i > 0)
        {
            EFI_MEMORY_DESCRIPTOR *prev = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)descs + (i - 1) * descSize);
            if (prev->NumberOfPages > 0)
            {
                HO_PHYSICAL_ADDRESS prevEnd = prev->PhysicalStart + prev->NumberOfPages * PAGE_4KB;
                if (start < prevEnd)
                {
                    klog(KLOG_LEVEL_ERROR, "PMM: overlapping descriptors at 0x%lx and 0x%lx\n", prev->PhysicalStart,
                         start);
                    return EC_ILLEGAL_ARGUMENT;
                }
            }
        }

        if (!IS_RECLAIMABLE_MEMORY(d->Type))
            continue;

        totalReclaimablePages += npages;
        if (start < managedLowest)
            managedLowest = start;
        if (end > managedHighest)
            managedHighest = end;
    }

    if (totalReclaimablePages == 0 || managedLowest >= managedHighest)
    {
        klog(KLOG_LEVEL_ERROR, "PMM: no reclaimable memory found\n");
        return EC_NOT_ENOUGH_MEMORY;
    }

    // The managed range spans from managedLowest to managedHighest.
    // Pages within this range that are NOT reclaimable will be marked reserved.
    uint64_t totalManagedPages = (managedHighest - managedLowest) / PAGE_4KB;

    // ---- Step 3: Calculate bitmap size and find placement ----
    // 2 bits per page, 4 pages per byte
    uint64_t bitmapBytes = (totalManagedPages + 3) / 4;
    uint64_t bitmapPages = (bitmapBytes + PAGE_4KB - 1) / PAGE_4KB;

    uint64_t ptPages = (capsule->PageTableInfo.Size + PAGE_4KB - 1) / PAGE_4KB;
    uint64_t fbPages = (capsule->FramebufferSize + PAGE_4KB - 1) / PAGE_4KB;

    BOOT_RESERVED_RANGE bootRanges[] = {
        {capsule->KrnlEntryPhys, capsule->PageLayout.KrnlPages},
        {capsule->KrnlStackPhys, capsule->PageLayout.KrnlStackPages},
        {capsule->KrnlIST1StackPhys, capsule->PageLayout.IST1StackPages},
        {capsule->BasePhys, capsule->PageLayout.HandoffPages},
        {capsule->PageTableInfo.Ptr, ptPages},
        {capsule->FramebufferPhys, fbPages},
    };

    // Find first reclaimable region large enough for bitmap and not overlapping boot-owned ranges
    HO_PHYSICAL_ADDRESS bitmapPhys = 0;
    BOOL bitmapPlaced = FALSE;

    for (size_t i = 0; i < descCount && !bitmapPlaced; i++)
    {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)descs + i * descSize);

        if (d->NumberOfPages == 0 || !IS_RECLAIMABLE_MEMORY(d->Type) || d->NumberOfPages < bitmapPages)
            continue;

        HO_PHYSICAL_ADDRESS descStart = d->PhysicalStart;
        HO_PHYSICAL_ADDRESS descEnd = descStart + d->NumberOfPages * PAGE_4KB;
        if (descEnd < descStart)
            continue;

        HO_PHYSICAL_ADDRESS firstCandidate = descStart;
        if (firstCandidate < PMM_LOW_RESERVED_BYTES)
            firstCandidate = PMM_LOW_RESERVED_BYTES;

        if (firstCandidate >= descEnd)
            continue;

        if (!HO_IS_ALIGNED(firstCandidate, PAGE_4KB))
            firstCandidate = HO_ALIGN_UP(firstCandidate, PAGE_4KB);

        HO_PHYSICAL_ADDRESS lastCandidate = descEnd - bitmapPages * PAGE_4KB;
        if (firstCandidate > lastCandidate)
            continue;

        for (HO_PHYSICAL_ADDRESS candidateStart = firstCandidate; candidateStart <= lastCandidate;
             candidateStart += PAGE_4KB)
        {
            HO_PHYSICAL_ADDRESS candidateEnd = candidateStart + bitmapPages * PAGE_4KB;

            if (candidateEnd < candidateStart || candidateEnd > descEnd)
                break;

            BOOL overlapsBootOwned = FALSE;
            for (size_t r = 0; r < sizeof(bootRanges) / sizeof(bootRanges[0]); r++)
            {
                if (bootRanges[r].Pages == 0)
                    continue;

                HO_PHYSICAL_ADDRESS rangeStart = bootRanges[r].Start;
                HO_PHYSICAL_ADDRESS rangeEnd = rangeStart + bootRanges[r].Pages * PAGE_4KB;
                if (rangeEnd < rangeStart)
                {
                    overlapsBootOwned = TRUE;
                    break;
                }

                if (PhysRangesOverlap(candidateStart, candidateEnd, rangeStart, rangeEnd))
                {
                    overlapsBootOwned = TRUE;
                    break;
                }
            }

            if (!overlapsBootOwned)
            {
                bitmapPhys = candidateStart;
                bitmapPlaced = TRUE;
                break;
            }
        }
    }

    if (!bitmapPlaced)
    {
        klog(KLOG_LEVEL_ERROR, "PMM: no safe region large enough for bitmap (%lu pages)\n", bitmapPages);
        return EC_NOT_ENOUGH_MEMORY;
    }

    // ---- Step 4: Initialize bitmap via HHDM ----
    uint8_t *bitmap = (uint8_t *)HHDM_PHYS2VIRT(bitmapPhys);
    memset(bitmap, 0, bitmapPages * PAGE_4KB); // All bits 0 = all pages free initially

    HO_STATUS status = KePmmBitmapSinkInit(&gBitmapCtx, bitmap, managedLowest, totalManagedPages);
    if (status != EC_SUCCESS)
        return status;

    // ---- Step 5: Mark non-reclaimable pages within managed range as reserved ----
    // Walk the managed range and mark pages that don't belong to any reclaimable region
    // Strategy: first mark ALL pages as reserved, then free back reclaimable regions
    for (uint64_t i = 0; i < totalManagedPages; i++)
    {
        BitmapSetState(&gBitmapCtx, i, PMM_PAGE_RESERVED);
    }
    gBitmapCtx.ReservedPages = totalManagedPages;
    gBitmapCtx.FreePages = 0;

    // Free back reclaimable regions
    for (size_t i = 0; i < descCount; i++)
    {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR *)((uint8_t *)descs + i * descSize);

        if (d->NumberOfPages == 0 || !IS_RECLAIMABLE_MEMORY(d->Type))
            continue;

        HO_PHYSICAL_ADDRESS start = d->PhysicalStart;
        uint64_t npages = d->NumberOfPages;
        HO_PHYSICAL_ADDRESS end = start + npages * PAGE_4KB;

        // Clip to managed range
        if (start < managedLowest)
            start = managedLowest;
        if (end > managedHighest)
            end = managedHighest;
        if (start >= end)
            continue;

        uint64_t startIdx = (start - managedLowest) / PAGE_4KB;
        uint64_t pageCount = (end - start) / PAGE_4KB;

        for (uint64_t j = 0; j < pageCount; j++)
        {
            BitmapSetState(&gBitmapCtx, startIdx + j, PMM_PAGE_FREE);
        }
        gBitmapCtx.ReservedPages -= pageCount;
        gBitmapCtx.FreePages += pageCount;
    }

    // ---- Step 6: Reserve bitmap metadata pages ----
    ReserveBootRange(&gBitmapCtx, bitmapPhys, bitmapPages);

    // ---- Step 7: Reserve legacy low memory window ----
    ReserveBootRange(&gBitmapCtx, 0, PMM_LOW_RESERVED_PAGES);

    // ---- Step 8: Reserve page 0 ----
    ReserveBootRange(&gBitmapCtx, 0, 1);

    // ---- Step 9: Reserve boot-time regions from BOOT_CAPSULE ----
    // Kernel image
    ReserveBootRange(&gBitmapCtx, capsule->KrnlEntryPhys, capsule->PageLayout.KrnlPages);

    // Kernel stack
    ReserveBootRange(&gBitmapCtx, capsule->KrnlStackPhys, capsule->PageLayout.KrnlStackPages);

    // IST1 stack
    ReserveBootRange(&gBitmapCtx, capsule->KrnlIST1StackPhys, capsule->PageLayout.IST1StackPages);

    // Boot handoff block (capsule header + manifest + memory map)
    ReserveBootRange(&gBitmapCtx, capsule->BasePhys, capsule->PageLayout.HandoffPages);

    // Page tables
    ReserveBootRange(&gBitmapCtx, capsule->PageTableInfo.Ptr, ptPages);

    // Framebuffer
    ReserveBootRange(&gBitmapCtx, capsule->FramebufferPhys, fbPages);

    // ---- Step 10: Activate device ----
    gPmmDevice.Sink = KePmmBitmapSinkGetSink();
    gPmmDevice.SinkContext = &gBitmapCtx;
    gPmmDevice.Initialized = TRUE;

    klog(KLOG_LEVEL_INFO, "PMM: initialized bitmap sink, managed [0x%lx - 0x%lx)\n", managedLowest, managedHighest);
    klog(KLOG_LEVEL_INFO, "PMM: total=%lu pages, free=%lu, reserved=%lu, bitmap=%lu pages @ 0x%lx\n", totalManagedPages,
         gBitmapCtx.FreePages, gBitmapCtx.ReservedPages, bitmapPages, bitmapPhys);

    return EC_SUCCESS;
}
