/**
 * HimuOperatingSystem
 *
 * File: ke/mm/kva.c
 * Description:
 * Ke Layer - Kernel virtual allocator, fixmap, and page-backed heap foundation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/mm.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

#define KE_KVA_STACK_ARENA_BASE   HO_ALIGN_UP((KRNL_IST1_STACK_VA + HO_STACK_SIZE + PAGE_4KB), PAGE_2MB)
#define KE_KVA_STACK_ARENA_SIZE   (32ULL * 1024ULL * 1024ULL)
#define KE_KVA_HEAP_ARENA_BASE    HO_ALIGN_UP((KE_KVA_STACK_ARENA_BASE + KE_KVA_STACK_ARENA_SIZE), PAGE_2MB)
#define KE_KVA_HEAP_ARENA_SIZE    (64ULL * 1024ULL * 1024ULL)
#define KE_KVA_FIXMAP_ARENA_SIZE  (64ULL * PAGE_4KB)
#define KE_KVA_FIXMAP_ARENA_BASE  (HHDM_BASE_VA - KE_KVA_FIXMAP_ARENA_SIZE)

#define KE_KVA_STACK_ARENA_PAGES  (KE_KVA_STACK_ARENA_SIZE / PAGE_4KB)
#define KE_KVA_FIXMAP_ARENA_PAGES (KE_KVA_FIXMAP_ARENA_SIZE / PAGE_4KB)
#define KE_KVA_HEAP_ARENA_PAGES   (KE_KVA_HEAP_ARENA_SIZE / PAGE_4KB)

#define KE_KVA_MAX_RANGES         512U
#define KE_KVA_PAGE_STATE_FREE    0U
#define KE_KVA_PAGE_STATE_ALLOC   1U
#define KE_KVA_PAGE_STATE_GUARD   2U
#define KE_KVA_DEFAULT_PAGE_ATTRS (PTE_WRITABLE | PTE_GLOBAL | PTE_NO_EXECUTE)
#define KE_TEMP_MAP_TOKEN_XOR     0xA5F00000U

typedef struct KE_KVA_ARENA_STATE
{
    const char *Name;
    HO_VIRTUAL_ADDRESS BaseAddress;
    uint64_t SizeBytes;
    uint64_t PageCount;
    uint8_t *PageStates;
} KE_KVA_ARENA_STATE;

typedef struct KE_KVA_RANGE_RECORD
{
    BOOL InUse;
    BOOL OwnsPhysicalBacking;
    KE_KVA_ARENA_TYPE Arena;
    uint32_t RecordId;
    uint64_t BasePageIndex;
    uint64_t TotalPages;
    uint64_t UsablePages;
    uint64_t GuardLowerPages;
    uint64_t GuardUpperPages;
} KE_KVA_RANGE_RECORD;

static KE_KVA_ARENA_STATE gKvaArenas[KE_KVA_ARENA_MAX];
static KE_KVA_RANGE_RECORD gKvaRanges[KE_KVA_MAX_RANGES];
static uint8_t gStackArenaStates[KE_KVA_STACK_ARENA_PAGES];
static uint8_t gFixmapArenaStates[KE_KVA_FIXMAP_ARENA_PAGES];
static uint8_t gHeapArenaStates[KE_KVA_HEAP_ARENA_PAGES];
static BOOL gKvaInitialized = FALSE;

static inline KE_KVA_ARENA_STATE *
KiKvaArenaState(KE_KVA_ARENA_TYPE arena)
{
    if (arena >= KE_KVA_ARENA_MAX)
        return NULL;
    return &gKvaArenas[arena];
}

static inline HO_VIRTUAL_ADDRESS
KiKvaRecordUsableBase(const KE_KVA_ARENA_STATE *arena, const KE_KVA_RANGE_RECORD *record)
{
    return arena->BaseAddress + (record->BasePageIndex + record->GuardLowerPages) * PAGE_4KB;
}

static HO_STATUS
KiKvaFillRangeFromRecord(const KE_KVA_RANGE_RECORD *record, KE_KVA_RANGE *outRange)
{
    if (!record || !outRange)
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(record->Arena);
    if (!arena)
        return EC_ILLEGAL_ARGUMENT;

    memset(outRange, 0, sizeof(*outRange));
    outRange->Arena = record->Arena;
    outRange->RecordId = record->RecordId;
    outRange->BaseAddress = arena->BaseAddress + record->BasePageIndex * PAGE_4KB;
    outRange->UsableBase = KiKvaRecordUsableBase(arena, record);
    outRange->TotalPages = record->TotalPages;
    outRange->UsablePages = record->UsablePages;
    outRange->GuardLowerPages = record->GuardLowerPages;
    outRange->GuardUpperPages = record->GuardUpperPages;
    return EC_SUCCESS;
}

static KE_KVA_RANGE_RECORD *
KiKvaFindRecordByUsableBase(HO_VIRTUAL_ADDRESS usableBase)
{
    for (uint32_t idx = 0; idx < KE_KVA_MAX_RANGES; ++idx)
    {
        KE_KVA_RANGE_RECORD *record = &gKvaRanges[idx];
        if (!record->InUse)
            continue;

        KE_KVA_ARENA_STATE *arena = KiKvaArenaState(record->Arena);
        if (!arena)
            continue;

        if (KiKvaRecordUsableBase(arena, record) == usableBase)
            return record;
    }

    return NULL;
}

static KE_KVA_RANGE_RECORD *
KiKvaFindRecordByAddress(KE_KVA_ARENA_TYPE arenaType, HO_VIRTUAL_ADDRESS virtAddr)
{
    for (uint32_t idx = 0; idx < KE_KVA_MAX_RANGES; ++idx)
    {
        KE_KVA_RANGE_RECORD *record = &gKvaRanges[idx];
        if (!record->InUse || record->Arena != arenaType)
            continue;

        KE_KVA_ARENA_STATE *arena = KiKvaArenaState(record->Arena);
        if (!arena)
            continue;

        HO_VIRTUAL_ADDRESS baseAddress = arena->BaseAddress + record->BasePageIndex * PAGE_4KB;
        HO_VIRTUAL_ADDRESS endExclusive = baseAddress + record->TotalPages * PAGE_4KB;
        if (virtAddr >= baseAddress && virtAddr < endExclusive)
            return record;
    }

    return NULL;
}

static KE_KVA_ADDRESS_KIND
KiKvaAddressKindFromArena(KE_KVA_ARENA_TYPE arenaType)
{
    switch (arenaType)
    {
    case KE_KVA_ARENA_STACK:
        return KE_KVA_ADDRESS_ACTIVE_STACK;
    case KE_KVA_ARENA_FIXMAP:
        return KE_KVA_ADDRESS_ACTIVE_FIXMAP;
    case KE_KVA_ARENA_HEAP:
        return KE_KVA_ADDRESS_ACTIVE_HEAP;
    default:
        return KE_KVA_ADDRESS_UNKNOWN;
    }
}

static BOOL
KiKvaLocateArenaByAddress(HO_VIRTUAL_ADDRESS virtAddr,
                          KE_KVA_ARENA_TYPE *outArenaType,
                          KE_KVA_ARENA_STATE **outArena,
                          uint64_t *outPageIndex)
{
    for (uint32_t idx = 0; idx < KE_KVA_ARENA_MAX; ++idx)
    {
        KE_KVA_ARENA_STATE *arena = &gKvaArenas[idx];
        HO_VIRTUAL_ADDRESS endExclusive = arena->BaseAddress + arena->SizeBytes;
        if (virtAddr < arena->BaseAddress || virtAddr >= endExclusive)
            continue;

        if (outArenaType)
            *outArenaType = (KE_KVA_ARENA_TYPE)idx;
        if (outArena)
            *outArena = arena;
        if (outPageIndex)
            *outPageIndex = (virtAddr - arena->BaseAddress) >> PAGE_SHIFT;
        return TRUE;
    }

    return FALSE;
}

static HO_STATUS
KiDecodeTempMapHandle(const KE_TEMP_PHYS_MAP_HANDLE *handle, uint32_t *outSlot)
{
    if (!handle || !outSlot)
        return EC_ILLEGAL_ARGUMENT;
    if (handle->Token == 0)
        return EC_INVALID_STATE;

    uint32_t encoded = handle->Token ^ KE_TEMP_MAP_TOKEN_XOR;
    if (encoded == 0 || encoded > KE_KVA_FIXMAP_ARENA_PAGES)
        return EC_ILLEGAL_ARGUMENT;

    *outSlot = encoded - 1U;
    return EC_SUCCESS;
}

static KE_KVA_RANGE_RECORD *
KiKvaFindRecordById(const KE_KVA_RANGE *range)
{
    if (!range || range->RecordId == 0 || range->RecordId > KE_KVA_MAX_RANGES)
        return NULL;

    KE_KVA_RANGE_RECORD *record = &gKvaRanges[range->RecordId - 1];
    if (!record->InUse)
        return NULL;
    if (record->Arena != range->Arena)
        return NULL;

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(record->Arena);
    if (!arena)
        return NULL;
    if (KiKvaRecordUsableBase(arena, record) != range->UsableBase)
        return NULL;

    return record;
}

static BOOL
KiKvaArenaOverlapsImportedRegions(const KE_KERNEL_ADDRESS_SPACE *space,
                                  HO_VIRTUAL_ADDRESS start,
                                  HO_VIRTUAL_ADDRESS endExclusive)
{
    if (!space)
        return TRUE;

    for (uint32_t idx = 0; idx < space->RegionCount; ++idx)
    {
        const KE_IMPORTED_REGION *region = &space->Regions[idx];
        if (endExclusive <= region->VirtualStart || start >= region->VirtualEndExclusive)
            continue;
        return TRUE;
    }

    return FALSE;
}

static HO_STATUS
KiKvaValidateArena(const KE_KVA_ARENA_STATE *arena, BOOL requireUnmapped)
{
    const KE_KERNEL_ADDRESS_SPACE *space = KeGetKernelAddressSpace();
    if (!space || !space->Initialized)
        return EC_INVALID_STATE;

    HO_VIRTUAL_ADDRESS endExclusive = arena->BaseAddress + arena->SizeBytes;
    if (KiKvaArenaOverlapsImportedRegions(space, arena->BaseAddress, endExclusive))
    {
        klog(KLOG_LEVEL_ERROR, "[KVA] arena %s overlaps imported boot mappings\n", arena->Name);
        return EC_INVALID_STATE;
    }

    if (!requireUnmapped)
        return EC_SUCCESS;

    for (uint64_t pageIdx = 0; pageIdx < arena->PageCount; ++pageIdx)
    {
        HO_VIRTUAL_ADDRESS virtAddr = arena->BaseAddress + pageIdx * PAGE_4KB;
        KE_PT_MAPPING mapping;
        HO_STATUS status = KePtQueryPage(space, virtAddr, &mapping);
        if (status != EC_SUCCESS)
            return status;
        if (mapping.Present)
        {
            klog(KLOG_LEVEL_ERROR, "[KVA] arena %s is already mapped at %p\n", arena->Name, (void *)(uint64_t)virtAddr);
            return EC_INVALID_STATE;
        }
    }

    return EC_SUCCESS;
}

static uint64_t
KiKvaCountFreePages(const KE_KVA_ARENA_STATE *arena)
{
    uint64_t freePages = 0;

    for (uint64_t pageIdx = 0; pageIdx < arena->PageCount; ++pageIdx)
    {
        if (arena->PageStates[pageIdx] == KE_KVA_PAGE_STATE_FREE)
            ++freePages;
    }

    return freePages;
}

static uint64_t
KiKvaCountActiveRanges(KE_KVA_ARENA_TYPE arenaType)
{
    uint64_t active = 0;

    for (uint32_t idx = 0; idx < KE_KVA_MAX_RANGES; ++idx)
    {
        if (gKvaRanges[idx].InUse && gKvaRanges[idx].Arena == arenaType)
            ++active;
    }

    return active;
}

static uint64_t
KiKvaCountAllActiveRanges(void)
{
    uint64_t active = 0;

    for (uint32_t idx = 0; idx < KE_KVA_MAX_RANGES; ++idx)
    {
        if (gKvaRanges[idx].InUse)
            ++active;
    }

    return active;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaInit(void)
{
    if (gKvaInitialized)
        return EC_INVALID_STATE;

    memset(gKvaRanges, 0, sizeof(gKvaRanges));
    memset(gStackArenaStates, 0, sizeof(gStackArenaStates));
    memset(gFixmapArenaStates, 0, sizeof(gFixmapArenaStates));
    memset(gHeapArenaStates, 0, sizeof(gHeapArenaStates));

    gKvaArenas[KE_KVA_ARENA_STACK] = (KE_KVA_ARENA_STATE){.Name = "stack",
                                                          .BaseAddress = KE_KVA_STACK_ARENA_BASE,
                                                          .SizeBytes = KE_KVA_STACK_ARENA_SIZE,
                                                          .PageCount = KE_KVA_STACK_ARENA_PAGES,
                                                          .PageStates = gStackArenaStates};
    gKvaArenas[KE_KVA_ARENA_FIXMAP] = (KE_KVA_ARENA_STATE){.Name = "fixmap",
                                                           .BaseAddress = KE_KVA_FIXMAP_ARENA_BASE,
                                                           .SizeBytes = KE_KVA_FIXMAP_ARENA_SIZE,
                                                           .PageCount = KE_KVA_FIXMAP_ARENA_PAGES,
                                                           .PageStates = gFixmapArenaStates};
    gKvaArenas[KE_KVA_ARENA_HEAP] = (KE_KVA_ARENA_STATE){.Name = "heap",
                                                         .BaseAddress = KE_KVA_HEAP_ARENA_BASE,
                                                         .SizeBytes = KE_KVA_HEAP_ARENA_SIZE,
                                                         .PageCount = KE_KVA_HEAP_ARENA_PAGES,
                                                         .PageStates = gHeapArenaStates};

    for (uint32_t idx = 0; idx < KE_KVA_ARENA_MAX; ++idx)
    {
        HO_STATUS status = KiKvaValidateArena(&gKvaArenas[idx], TRUE);
        if (status != EC_SUCCESS)
            return status;
    }

    for (uint32_t idx = 0; idx < KE_KVA_MAX_RANGES; ++idx)
        gKvaRanges[idx].RecordId = idx + 1;

    gKvaInitialized = TRUE;

    for (uint32_t idx = 0; idx < KE_KVA_ARENA_MAX; ++idx)
    {
        const KE_KVA_ARENA_STATE *arena = &gKvaArenas[idx];
        klog(KLOG_LEVEL_INFO, "[KVA] arena %-6s va=[%p,%p) pages=%lu\n", arena->Name,
             (void *)(uint64_t)arena->BaseAddress, (void *)(uint64_t)(arena->BaseAddress + arena->SizeBytes),
             (unsigned long)arena->PageCount);
    }

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaAllocRange(KE_KVA_ARENA_TYPE arenaType,
                uint64_t usablePages,
                uint64_t guardLowerPages,
                uint64_t guardUpperPages,
                BOOL ownsPhysicalBacking,
                KE_KVA_RANGE *outRange)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outRange || usablePages == 0)
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(arenaType);
    if (!arena)
        return EC_ILLEGAL_ARGUMENT;

    uint64_t totalPages = guardLowerPages + usablePages + guardUpperPages;
    if (totalPages == 0 || totalPages > arena->PageCount)
        return EC_OUT_OF_RESOURCE;

    KE_KVA_RANGE_RECORD *record = NULL;
    for (uint32_t idx = 0; idx < KE_KVA_MAX_RANGES; ++idx)
    {
        if (!gKvaRanges[idx].InUse)
        {
            record = &gKvaRanges[idx];
            break;
        }
    }
    if (!record)
        return EC_OUT_OF_RESOURCE;

    for (uint64_t basePage = 0; basePage + totalPages <= arena->PageCount; ++basePage)
    {
        BOOL available = TRUE;
        for (uint64_t offset = 0; offset < totalPages; ++offset)
        {
            if (arena->PageStates[basePage + offset] != KE_KVA_PAGE_STATE_FREE)
            {
                available = FALSE;
                break;
            }
        }
        if (!available)
            continue;

        for (uint64_t offset = 0; offset < totalPages; ++offset)
        {
            uint64_t pageIndex = basePage + offset;
            BOOL isGuard = offset < guardLowerPages || offset >= (guardLowerPages + usablePages);
            arena->PageStates[pageIndex] = isGuard ? KE_KVA_PAGE_STATE_GUARD : KE_KVA_PAGE_STATE_ALLOC;
        }

        memset(record, 0, sizeof(*record));
        record->InUse = TRUE;
        record->OwnsPhysicalBacking = ownsPhysicalBacking;
        record->Arena = arenaType;
        record->RecordId = (uint32_t)(record - gKvaRanges) + 1;
        record->BasePageIndex = basePage;
        record->TotalPages = totalPages;
        record->UsablePages = usablePages;
        record->GuardLowerPages = guardLowerPages;
        record->GuardUpperPages = guardUpperPages;

        return KiKvaFillRangeFromRecord(record, outRange);
    }

    return EC_OUT_OF_RESOURCE;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaMapPage(const KE_KVA_RANGE *range, uint64_t usablePageIndex, HO_PHYSICAL_ADDRESS physAddr, uint64_t attributes)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!range)
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordById(range);
    if (!record)
        return EC_INVALID_STATE;
    if (usablePageIndex >= record->UsablePages)
        return EC_ILLEGAL_ARGUMENT;

    HO_VIRTUAL_ADDRESS virtAddr = range->UsableBase + usablePageIndex * PAGE_4KB;
    return KePtMapPage(KeGetKernelAddressSpace(), virtAddr, physAddr, attributes);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaMapOwnedPages(const KE_KVA_RANGE *range, uint64_t attributes)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!range)
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordById(range);
    if (!record || !record->OwnsPhysicalBacking)
        return EC_INVALID_STATE;

    for (uint64_t pageIdx = 0; pageIdx < record->UsablePages; ++pageIdx)
    {
        HO_PHYSICAL_ADDRESS physAddr = 0;
        HO_STATUS status = KePmmAllocPages(1, NULL, &physAddr);
        if (status != EC_SUCCESS)
        {
            HO_STATUS cleanupStatus = KeKvaReleaseRange(range->UsableBase);
            if (cleanupStatus != EC_SUCCESS)
                return cleanupStatus;
            return status;
        }

        status = KeKvaMapPage(range, pageIdx, physAddr, attributes);
        if (status != EC_SUCCESS)
        {
            (void)KePmmFreePages(physAddr, 1);
            HO_STATUS cleanupStatus = KeKvaReleaseRange(range->UsableBase);
            if (cleanupStatus != EC_SUCCESS)
                return cleanupStatus;
            return status;
        }
    }

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaReleaseRange(HO_VIRTUAL_ADDRESS usableBase)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!HO_IS_ALIGNED(usableBase, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordByUsableBase(usableBase);
    if (!record)
        return EC_INVALID_STATE;

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(record->Arena);
    if (!arena)
        return EC_INVALID_STATE;

    for (uint64_t pageIdx = 0; pageIdx < record->UsablePages; ++pageIdx)
    {
        HO_VIRTUAL_ADDRESS virtAddr = usableBase + pageIdx * PAGE_4KB;
        KE_PT_MAPPING mapping;
        HO_STATUS status = KePtQueryPage(KeGetKernelAddressSpace(), virtAddr, &mapping);
        if (status != EC_SUCCESS)
            return status;
        if (!mapping.Present)
            continue;
        if (mapping.Level != 1)
            return EC_NOT_SUPPORTED;

        status = KePtUnmapPage(KeGetKernelAddressSpace(), virtAddr);
        if (status != EC_SUCCESS)
            return status;

        if (record->OwnsPhysicalBacking)
        {
            status = KePmmFreePages(mapping.PhysicalBase, 1);
            if (status != EC_SUCCESS)
                return status;
        }
    }

    for (uint64_t pageIdx = 0; pageIdx < record->TotalPages; ++pageIdx)
        arena->PageStates[record->BasePageIndex + pageIdx] = KE_KVA_PAGE_STATE_FREE;

    memset(record, 0, sizeof(*record));
    record->RecordId = (uint32_t)(record - gKvaRanges) + 1;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaQueryRange(HO_VIRTUAL_ADDRESS usableBase, KE_KVA_RANGE *outRange)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outRange)
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordByUsableBase(usableBase);
    if (!record)
        return EC_INVALID_STATE;

    return KiKvaFillRangeFromRecord(record, outRange);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaQueryArenaInfo(KE_KVA_ARENA_TYPE arenaType, KE_KVA_ARENA_INFO *outInfo)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(arenaType);
    if (!arena)
        return EC_ILLEGAL_ARGUMENT;

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->Arena = arenaType;
    outInfo->BaseAddress = arena->BaseAddress;
    outInfo->EndAddressExclusive = arena->BaseAddress + arena->SizeBytes;
    outInfo->TotalPages = arena->PageCount;
    outInfo->FreePages = KiKvaCountFreePages(arena);
    outInfo->ActiveAllocations = KiKvaCountActiveRanges(arenaType);
    outInfo->OverlapsImportedRegions = KiKvaArenaOverlapsImportedRegions(
        KeGetKernelAddressSpace(), outInfo->BaseAddress, outInfo->EndAddressExclusive);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaQueryUsageInfo(KE_KVA_USAGE_INFO *outInfo)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->ActiveRangeCount = KiKvaCountAllActiveRanges();
    outInfo->FixmapTotalSlots = gKvaArenas[KE_KVA_ARENA_FIXMAP].PageCount;
    outInfo->FixmapActiveSlots = KiKvaCountActiveRanges(KE_KVA_ARENA_FIXMAP);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaClassifyAddress(HO_VIRTUAL_ADDRESS virtAddr, KE_KVA_ADDRESS_INFO *outInfo)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->Kind = KE_KVA_ADDRESS_OUTSIDE;
    outInfo->Arena = KE_KVA_ARENA_MAX;

    KE_KVA_ARENA_TYPE arenaType = KE_KVA_ARENA_MAX;
    KE_KVA_ARENA_STATE *arena = NULL;
    uint64_t pageIndex = 0;
    if (!KiKvaLocateArenaByAddress(virtAddr, &arenaType, &arena, &pageIndex))
        return EC_SUCCESS;

    outInfo->InKvaArena = TRUE;
    outInfo->Arena = arenaType;
    outInfo->ArenaBase = arena->BaseAddress;
    outInfo->ArenaEndExclusive = arena->BaseAddress + arena->SizeBytes;
    outInfo->ArenaPageIndex = pageIndex;

    uint8_t pageState = arena->PageStates[pageIndex];
    if (pageState == KE_KVA_PAGE_STATE_FREE)
    {
        outInfo->Kind = KE_KVA_ADDRESS_FREE_HOLE;
        return EC_SUCCESS;
    }

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordByAddress(arenaType, virtAddr);
    if (!record)
    {
        if (pageState == KE_KVA_PAGE_STATE_GUARD)
        {
            outInfo->Kind = KE_KVA_ADDRESS_GUARD_PAGE;
        }
        else if (pageState == KE_KVA_PAGE_STATE_ALLOC)
        {
            outInfo->Kind = KiKvaAddressKindFromArena(arenaType);
        }
        else
        {
            outInfo->Kind = KE_KVA_ADDRESS_UNKNOWN;
        }
        return EC_INVALID_STATE;
    }

    HO_STATUS status = KiKvaFillRangeFromRecord(record, &outInfo->Range);
    if (status != EC_SUCCESS)
        return status;
    outInfo->HasRange = TRUE;

    if (pageState == KE_KVA_PAGE_STATE_GUARD)
    {
        outInfo->Kind = KE_KVA_ADDRESS_GUARD_PAGE;
        return EC_SUCCESS;
    }

    if (pageState == KE_KVA_PAGE_STATE_ALLOC)
    {
        outInfo->Kind = KiKvaAddressKindFromArena(arenaType);
        return outInfo->Kind == KE_KVA_ADDRESS_UNKNOWN ? EC_INVALID_STATE : EC_SUCCESS;
    }

    outInfo->Kind = KE_KVA_ADDRESS_UNKNOWN;
    return EC_INVALID_STATE;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaValidateLayout(void)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;

    for (uint32_t idx = 0; idx < KE_KVA_ARENA_MAX; ++idx)
    {
        HO_STATUS status = KiKvaValidateArena(&gKvaArenas[idx], FALSE);
        if (status != EC_SUCCESS)
            return status;
    }

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeFixmapAcquire(HO_PHYSICAL_ADDRESS physAddr, uint64_t attributes, uint32_t *outSlot, HO_VIRTUAL_ADDRESS *outVirtAddr)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outSlot || !outVirtAddr || !HO_IS_ALIGNED(physAddr, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_RANGE range;
    HO_STATUS status = KeKvaAllocRange(KE_KVA_ARENA_FIXMAP, 1, 0, 0, FALSE, &range);
    if (status != EC_SUCCESS)
        return status;

    status = KeKvaMapPage(&range, 0, physAddr, attributes);
    if (status != EC_SUCCESS)
    {
        HO_STATUS cleanupStatus = KeKvaReleaseRange(range.UsableBase);
        if (cleanupStatus != EC_SUCCESS)
            return cleanupStatus;
        return status;
    }

    *outSlot = (uint32_t)((range.UsableBase - KE_KVA_FIXMAP_ARENA_BASE) / PAGE_4KB);
    *outVirtAddr = range.UsableBase;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeFixmapRelease(uint32_t slot)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (slot >= KE_KVA_FIXMAP_ARENA_PAGES)
        return EC_ILLEGAL_ARGUMENT;

    HO_VIRTUAL_ADDRESS usableBase = KE_KVA_FIXMAP_ARENA_BASE + (uint64_t)slot * PAGE_4KB;
    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordByUsableBase(usableBase);
    if (!record || record->Arena != KE_KVA_ARENA_FIXMAP)
        return EC_INVALID_STATE;

    return KeKvaReleaseRange(usableBase);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeTempPhysMapAcquire(HO_PHYSICAL_ADDRESS physAddr,
                     uint64_t attributes,
                     KE_TEMP_PHYS_MAP_HANDLE *outHandle,
                     HO_VIRTUAL_ADDRESS *outVirtAddr)
{
    if (!outHandle || !outVirtAddr || !HO_IS_ALIGNED(physAddr, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;

    uint32_t slot = 0;
    HO_STATUS status = KeFixmapAcquire(physAddr, attributes, &slot, outVirtAddr);
    if (status != EC_SUCCESS)
        return status;

    outHandle->Token = (slot + 1U) ^ KE_TEMP_MAP_TOKEN_XOR;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeTempPhysMapRelease(KE_TEMP_PHYS_MAP_HANDLE *handle)
{
    if (!handle)
        return EC_ILLEGAL_ARGUMENT;

    uint32_t slot = 0;
    HO_STATUS status = KiDecodeTempMapHandle(handle, &slot);
    if (status != EC_SUCCESS)
        return status;

    status = KeFixmapRelease(slot);
    if (status == EC_SUCCESS)
        handle->Token = 0;
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeHeapAllocPages(uint64_t pageCount, HO_VIRTUAL_ADDRESS *outVirtAddr)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outVirtAddr || pageCount == 0)
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_RANGE range;
    HO_STATUS status = KeKvaAllocRange(KE_KVA_ARENA_HEAP, pageCount, 0, 0, TRUE, &range);
    if (status != EC_SUCCESS)
        return status;

    status = KeKvaMapOwnedPages(&range, KE_KVA_DEFAULT_PAGE_ATTRS);
    if (status != EC_SUCCESS)
        return status;

    *outVirtAddr = range.UsableBase;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeHeapFreePages(HO_VIRTUAL_ADDRESS baseVirt)
{
    return KeKvaReleaseRange(baseVirt);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaSelfTest(void)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;

    HO_STATUS status = KeKvaValidateLayout();
    if (status != EC_SUCCESS)
        return status;

    KE_KVA_RANGE stackRange;
    status = KeKvaAllocRange(KE_KVA_ARENA_STACK, 4, 1, 0, TRUE, &stackRange);
    if (status != EC_SUCCESS)
        return status;

    status = KeKvaMapOwnedPages(&stackRange, KE_KVA_DEFAULT_PAGE_ATTRS);
    if (status != EC_SUCCESS)
        return status;

    KE_PT_MAPPING mapping;
    status = KePtQueryPage(KeGetKernelAddressSpace(), stackRange.BaseAddress, &mapping);
    if (status != EC_SUCCESS)
        return status;
    if (mapping.Present)
        return EC_INVALID_STATE;

    status = KePtQueryPage(KeGetKernelAddressSpace(), stackRange.UsableBase, &mapping);
    if (status != EC_SUCCESS)
        return status;
    if (!mapping.Present || mapping.Level != 1)
        return EC_INVALID_STATE;

    status = KeKvaReleaseRange(stackRange.UsableBase);
    if (status != EC_SUCCESS)
        return status;

    HO_PHYSICAL_ADDRESS testPhys = 0;
    status = KePmmAllocPages(1, NULL, &testPhys);
    if (status != EC_SUCCESS)
        return status;

    // HHDM self-check remains in the diagnostics allowlist on purpose.
    volatile uint64_t *alias = (volatile uint64_t *)(uint64_t)HHDM_PHYS2VIRT(testPhys);
    *alias = 0x6B56414649584D50ULL;

    uint32_t slotA = 0;
    uint32_t slotB = 0;
    HO_VIRTUAL_ADDRESS fixVirtA = 0;
    HO_VIRTUAL_ADDRESS fixVirtB = 0;

    status = KeFixmapAcquire(testPhys, KE_KVA_DEFAULT_PAGE_ATTRS, &slotA, &fixVirtA);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;

    volatile uint64_t *fixAliasA = (volatile uint64_t *)(uint64_t)fixVirtA;
    if (*fixAliasA != *alias)
    {
        status = EC_INVALID_STATE;
        goto cleanup_fixmap_slot_a;
    }

    status = KeFixmapRelease(slotA);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;

    status = KeFixmapAcquire(testPhys, KE_KVA_DEFAULT_PAGE_ATTRS, &slotB, &fixVirtB);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;

    volatile uint64_t *fixAliasB = (volatile uint64_t *)(uint64_t)fixVirtB;
    if (*fixAliasB != *alias)
    {
        status = EC_INVALID_STATE;
        goto cleanup_fixmap_slot_b;
    }

    status = KeFixmapRelease(slotB);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;

    HO_VIRTUAL_ADDRESS heapVirt = 0;
    status = KeHeapAllocPages(2, &heapVirt);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;

    volatile uint64_t *heapWords = (volatile uint64_t *)(uint64_t)heapVirt;
    heapWords[0] = 0x484541504B564131ULL;
    heapWords[512] = 0x484541504B564132ULL;
    if (heapWords[0] != 0x484541504B564131ULL || heapWords[512] != 0x484541504B564132ULL)
    {
        status = EC_INVALID_STATE;
        goto cleanup_heap;
    }

    status = KeHeapFreePages(heapVirt);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;

    status = KePmmFreePages(testPhys, 1);
    if (status != EC_SUCCESS)
        return status;

    klog(KLOG_LEVEL_INFO, "[KVA] self-test OK: guard pages, fixmap reuse, and heap growth verified\n");
    return EC_SUCCESS;

cleanup_heap:
    if (heapVirt != 0)
    {
        HO_STATUS cleanupStatus = KeHeapFreePages(heapVirt);
        if (cleanupStatus != EC_SUCCESS)
            status = cleanupStatus;
    }
cleanup_fixmap_slot_b:
    if (fixVirtB != 0)
    {
        HO_STATUS cleanupStatus = KeFixmapRelease(slotB);
        if (cleanupStatus != EC_SUCCESS)
            status = cleanupStatus;
    }
cleanup_fixmap_slot_a:
    if (fixVirtA != 0)
    {
        HO_STATUS cleanupStatus = KeFixmapRelease(slotA);
        if (cleanupStatus != EC_SUCCESS)
            status = cleanupStatus;
    }
cleanup_fixmap_phys:
    (void)KePmmFreePages(testPhys, 1);
    return status;
}
