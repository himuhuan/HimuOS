/**
 * HimuOperatingSystem
 *
 * File: ke/pmm/bitmap_sink.c
 * Description:
 * Ke Layer - Bitmap-based PMM sink implementation
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "bitmap_sink.h"

// ============================================================================
// 2-bit bitmap helpers
// Each byte holds 4 page states: bits [1:0] = page i, [3:2] = page i+1, etc.
// ============================================================================

static inline KE_PMM_PAGE_STATE
BitmapGetState(const KE_PMM_BITMAP_CONTEXT *ctx, uint64_t pageIndex)
{
    uint64_t byteIdx = pageIndex >> 2;
    uint64_t bitOff = (pageIndex & 0x3) << 1;
    return (KE_PMM_PAGE_STATE)((ctx->Bitmap[byteIdx] >> bitOff) & 0x3);
}

static inline void
BitmapSetState(KE_PMM_BITMAP_CONTEXT *ctx, uint64_t pageIndex, KE_PMM_PAGE_STATE state)
{
    uint64_t byteIdx = pageIndex >> 2;
    uint64_t bitOff = (pageIndex & 0x3) << 1;
    ctx->Bitmap[byteIdx] = (uint8_t)((ctx->Bitmap[byteIdx] & ~(0x3 << bitOff)) | ((uint8_t)state << bitOff));
}

static void
BitmapSetRange(KE_PMM_BITMAP_CONTEXT *ctx, uint64_t startIndex, uint64_t count, KE_PMM_PAGE_STATE state)
{
    // TODO：Performance improvement
    for (uint64_t i = 0; i < count; i++)
    {
        BitmapSetState(ctx, startIndex + i, state);
    }
}

static BOOL
BitmapCheckRange(const KE_PMM_BITMAP_CONTEXT *ctx, uint64_t startIndex, uint64_t count, KE_PMM_PAGE_STATE expectedState)
{
    // TODO：Performance improvement
    for (uint64_t i = 0; i < count; i++)
    {
        if (BitmapGetState(ctx, startIndex + i) != expectedState)
            return FALSE;
    }
    return TRUE;
}

// ============================================================================
// Sink operations
// ============================================================================

static HO_STATUS
BitmapAllocPages(void *self,
                 uint64_t count,
                 const KE_PMM_ALLOC_CONSTRAINTS *constraints,
                 HO_PHYSICAL_ADDRESS *outBasePhys)
{
    KE_PMM_BITMAP_CONTEXT *ctx = (KE_PMM_BITMAP_CONTEXT *)self;

    if (count == 0)
        return EC_ILLEGAL_ARGUMENT;

    uint64_t alignPages = 1;
    uint64_t maxPageIndex = ctx->TotalManagedPages;

    if (constraints)
    {
        if (constraints->AlignmentPages > 1)
        {
            // Must be power of two
            if ((constraints->AlignmentPages & (constraints->AlignmentPages - 1)) != 0)
                return EC_ILLEGAL_ARGUMENT;
            alignPages = constraints->AlignmentPages;
        }
        if (constraints->MaxPhysAddr != 0)
        {
            // Calculate the maximum page index (exclusive) that fits within MaxPhysAddr
            uint64_t maxAddr = constraints->MaxPhysAddr;
            if (maxAddr < ctx->ManagedBasePhys)
                return EC_NOT_ENOUGH_MEMORY;
            uint64_t limit = (maxAddr - ctx->ManagedBasePhys + 1) >> PAGE_SHIFT;
            if (limit < maxPageIndex)
                maxPageIndex = limit;
        }
    }

    if (count > maxPageIndex)
        return EC_NOT_ENOUGH_MEMORY;

    // First-fit scan from lowest index
    uint64_t i = 0;
    while (i + count <= maxPageIndex)
    {
        if (alignPages > 1)
        {
            uint64_t rem = i % alignPages;
            if (rem != 0)
            {
                i += alignPages - rem;
                continue;
            }
        }

        BOOL found = TRUE;
        for (uint64_t j = 0; j < count; j++)
        {
            if (BitmapGetState(ctx, i + j) != PMM_PAGE_FREE)
            {
                // Skip past the non-free page
                i = i + j + 1;
                found = FALSE;
                break;
            }
        }

        if (found)
        {
            // Mark as allocated
            BitmapSetRange(ctx, i, count, PMM_PAGE_ALLOCATED);
            ctx->FreePages -= count;
            ctx->AllocatedPages += count;
            *outBasePhys = ctx->ManagedBasePhys + i * PAGE_4KB;
            return EC_SUCCESS;
        }
    }

    return EC_NOT_ENOUGH_MEMORY;
}

static HO_STATUS
BitmapFreePages(void *self, HO_PHYSICAL_ADDRESS basePhys, uint64_t count)
{
    KE_PMM_BITMAP_CONTEXT *ctx = (KE_PMM_BITMAP_CONTEXT *)self;

    if (count == 0)
        return EC_ILLEGAL_ARGUMENT;
    if (!HO_IS_ALIGNED(basePhys, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;
    if (basePhys < ctx->ManagedBasePhys)
        return EC_ILLEGAL_ARGUMENT;

    uint64_t startIndex = (basePhys - ctx->ManagedBasePhys) >> PAGE_SHIFT;
    if (startIndex + count > ctx->TotalManagedPages)
        return EC_ILLEGAL_ARGUMENT;

    // Validate all pages are allocated
    if (!BitmapCheckRange(ctx, startIndex, count, PMM_PAGE_ALLOCATED))
        return EC_INVALID_STATE;

    BitmapSetRange(ctx, startIndex, count, PMM_PAGE_FREE);
    ctx->AllocatedPages -= count;
    ctx->FreePages += count;
    return EC_SUCCESS;
}

static HO_STATUS
BitmapReservePages(void *self, HO_PHYSICAL_ADDRESS basePhys, uint64_t count)
{
    KE_PMM_BITMAP_CONTEXT *ctx = (KE_PMM_BITMAP_CONTEXT *)self;

    if (count == 0)
        return EC_ILLEGAL_ARGUMENT;
    if (!HO_IS_ALIGNED(basePhys, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;
    if (basePhys < ctx->ManagedBasePhys)
        return EC_ILLEGAL_ARGUMENT;

    uint64_t startIndex = (basePhys - ctx->ManagedBasePhys) >> PAGE_SHIFT;
    if (startIndex + count > ctx->TotalManagedPages)
        return EC_ILLEGAL_ARGUMENT;

    // Validate all pages are free
    if (!BitmapCheckRange(ctx, startIndex, count, PMM_PAGE_FREE))
        return EC_INVALID_STATE;

    BitmapSetRange(ctx, startIndex, count, PMM_PAGE_RESERVED);
    ctx->FreePages -= count;
    ctx->ReservedPages += count;
    return EC_SUCCESS;
}

static HO_STATUS
BitmapQueryStats(void *self, KE_PMM_STATS *outStats)
{
    KE_PMM_BITMAP_CONTEXT *ctx = (KE_PMM_BITMAP_CONTEXT *)self;

    if (!outStats)
        return EC_ILLEGAL_ARGUMENT;

    outStats->TotalBytes = ctx->TotalManagedPages * PAGE_4KB;
    outStats->FreeBytes = ctx->FreePages * PAGE_4KB;
    outStats->AllocatedBytes = ctx->AllocatedPages * PAGE_4KB;
    outStats->ReservedBytes = ctx->ReservedPages * PAGE_4KB;
    return EC_SUCCESS;
}

static HO_STATUS
BitmapCheckInvariants(void *self)
{
    KE_PMM_BITMAP_CONTEXT *ctx = (KE_PMM_BITMAP_CONTEXT *)self;

    uint64_t countFree = 0, countAlloc = 0, countReserved = 0;
    for (uint64_t i = 0; i < ctx->TotalManagedPages; i++)
    {
        KE_PMM_PAGE_STATE s = BitmapGetState(ctx, i);
        switch (s)
        {
        case PMM_PAGE_FREE:
            countFree++;
            break;
        case PMM_PAGE_ALLOCATED:
            countAlloc++;
            break;
        case PMM_PAGE_RESERVED:
            countReserved++;
            break;
        default:
            return EC_INVALID_STATE;
        }
    }

    if (countFree != ctx->FreePages || countAlloc != ctx->AllocatedPages || countReserved != ctx->ReservedPages)
        return EC_INVALID_STATE;

    if (countFree + countAlloc + countReserved != ctx->TotalManagedPages)
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

// ============================================================================
// Sink table & init
// ============================================================================

static KE_PMM_SINK gBitmapSink = {
    .AllocPages = BitmapAllocPages,
    .FreePages = BitmapFreePages,
    .ReservePages = BitmapReservePages,
    .QueryStats = BitmapQueryStats,
    .CheckInvariants = BitmapCheckInvariants,
};

HO_KERNEL_API HO_STATUS
KePmmBitmapSinkInit(KE_PMM_BITMAP_CONTEXT *ctx,
                    uint8_t *bitmap,
                    HO_PHYSICAL_ADDRESS managedBasePhys,
                    uint64_t totalManagedPages)
{
    if (!ctx || !bitmap || totalManagedPages == 0)
        return EC_ILLEGAL_ARGUMENT;

    ctx->Bitmap = bitmap;
    ctx->ManagedBasePhys = managedBasePhys;
    ctx->TotalManagedPages = totalManagedPages;
    ctx->FreePages = 0;
    ctx->AllocatedPages = 0;
    ctx->ReservedPages = 0;
    return EC_SUCCESS;
}

HO_KERNEL_API KE_PMM_SINK *
KePmmBitmapSinkGetSink(void)
{
    return &gBitmapSink;
}
