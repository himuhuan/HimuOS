/**
 * HimuOperatingSystem
 *
 * File: ke/mm/kva.c
 * Description:
 * Ke Layer - Kernel virtual allocator, fixmap, and page-backed heap foundation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/critical_section.h>
#include <kernel/ke/mm.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

#define KE_KVA_STACK_ARENA_BASE           HO_ALIGN_UP((KRNL_IST2_STACK_VA + HO_STACK_SIZE + PAGE_4KB), PAGE_2MB)
#define KE_KVA_STACK_ARENA_SIZE           (32ULL * 1024ULL * 1024ULL)
#define KE_KVA_HEAP_ARENA_BASE            HO_ALIGN_UP((KE_KVA_STACK_ARENA_BASE + KE_KVA_STACK_ARENA_SIZE), PAGE_2MB)
#define KE_KVA_HEAP_ARENA_SIZE            (64ULL * 1024ULL * 1024ULL)
#define KE_KVA_FIXMAP_ARENA_SIZE          (64ULL * PAGE_4KB)
#define KE_KVA_FIXMAP_ARENA_BASE          (HHDM_BASE_VA - KE_KVA_FIXMAP_ARENA_SIZE)

#define KE_KVA_STACK_ARENA_PAGES          (KE_KVA_STACK_ARENA_SIZE / PAGE_4KB)
#define KE_KVA_FIXMAP_ARENA_PAGES         (KE_KVA_FIXMAP_ARENA_SIZE / PAGE_4KB)
#define KE_KVA_HEAP_ARENA_PAGES           (KE_KVA_HEAP_ARENA_SIZE / PAGE_4KB)

#define KE_KVA_MAX_RANGES                 512U
#define KE_KVA_PAGE_STATE_FREE            0U
#define KE_KVA_PAGE_STATE_ALLOC           1U
#define KE_KVA_PAGE_STATE_GUARD           2U
#define KE_KVA_PAGE_STATE_RETIRED         3U
#define KE_KVA_DEFAULT_PAGE_ATTRS         (PTE_WRITABLE | PTE_GLOBAL | PTE_NO_EXECUTE)
#define KE_TEMP_MAP_TOKEN_SLOT_BITS       6U
#define KE_TEMP_MAP_TOKEN_SLOT_MASK       ((1ULL << KE_TEMP_MAP_TOKEN_SLOT_BITS) - 1ULL)
#define KE_TEMP_MAP_TOKEN_GENERATION_MASK ((1ULL << (64U - KE_TEMP_MAP_TOKEN_SLOT_BITS)) - 1ULL)

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
    uint64_t Generation;
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
static uint64_t gFixmapSlotGenerations[KE_KVA_FIXMAP_ARENA_PAGES];
static uint64_t gKvaRangeGenerationCounter = 1ULL;
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

static inline BOOL
KiKvaFixmapSlotRetired(uint64_t pageIndex)
{
    return pageIndex < KE_KVA_FIXMAP_ARENA_PAGES &&
           gFixmapSlotGenerations[pageIndex] >= KE_TEMP_MAP_TOKEN_GENERATION_MASK;
}

static uint64_t
KiKvaNextRangeGeneration(void)
{
    if (gKvaRangeGenerationCounter == 0)
        return 0;

    uint64_t generation = gKvaRangeGenerationCounter;
    if (generation == 0xFFFFFFFFFFFFFFFFULL)
        gKvaRangeGenerationCounter = 0;
    else
        gKvaRangeGenerationCounter++;

    return generation;
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
    outRange->Generation = record->Generation;
    outRange->BaseAddress = arena->BaseAddress + record->BasePageIndex * PAGE_4KB;
    outRange->UsableBase = KiKvaRecordUsableBase(arena, record);
    outRange->TotalPages = record->TotalPages;
    outRange->UsablePages = record->UsablePages;
    outRange->GuardLowerPages = record->GuardLowerPages;
    outRange->GuardUpperPages = record->GuardUpperPages;
    return EC_SUCCESS;
}

static HO_STATUS
KiKvaFillActiveRangeEntry(const KE_KVA_RANGE_RECORD *record, KE_KVA_ACTIVE_RANGE_ENTRY *outEntry)
{
    if (!record || !outEntry)
        return EC_ILLEGAL_ARGUMENT;

    KE_KVA_RANGE range;
    HO_STATUS status = KiKvaFillRangeFromRecord(record, &range);
    if (status != EC_SUCCESS)
        return status;

    memset(outEntry, 0, sizeof(*outEntry));
    outEntry->Arena = range.Arena;
    outEntry->RecordId = range.RecordId;
    outEntry->Generation = range.Generation;
    outEntry->BaseAddress = range.BaseAddress;
    outEntry->EndAddressExclusive = range.BaseAddress + range.TotalPages * PAGE_4KB;
    outEntry->UsableBase = range.UsableBase;
    outEntry->UsableEndExclusive = range.UsableBase + range.UsablePages * PAGE_4KB;
    outEntry->TotalPages = range.TotalPages;
    outEntry->UsablePages = range.UsablePages;
    outEntry->GuardLowerPages = range.GuardLowerPages;
    outEntry->GuardUpperPages = range.GuardUpperPages;
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
KiDecodeTempMapHandle(const KE_TEMP_PHYS_MAP_HANDLE *handle, uint32_t *outSlot, uint64_t *outGeneration)
{
    if (!handle || !outSlot || !outGeneration)
        return EC_ILLEGAL_ARGUMENT;
    if (handle->Token == 0)
        return EC_INVALID_STATE;

    uint32_t slot = (uint32_t)(handle->Token & KE_TEMP_MAP_TOKEN_SLOT_MASK);
    uint64_t generation = handle->Token >> KE_TEMP_MAP_TOKEN_SLOT_BITS;
    if (slot >= KE_KVA_FIXMAP_ARENA_PAGES || generation == 0 || generation > KE_TEMP_MAP_TOKEN_GENERATION_MASK)
        return EC_ILLEGAL_ARGUMENT;

    *outSlot = slot;
    *outGeneration = generation;
    return EC_SUCCESS;
}

static uint64_t
KiNextFixmapSlotGeneration(uint32_t slot)
{
    if (slot >= KE_KVA_FIXMAP_ARENA_PAGES)
        return 0;
    if (gFixmapSlotGenerations[slot] >= KE_TEMP_MAP_TOKEN_GENERATION_MASK)
        return 0;

    gFixmapSlotGenerations[slot]++;
    return gFixmapSlotGenerations[slot];
}

static uint64_t
KiEncodeTempMapToken(uint32_t slot, uint64_t generation)
{
    if (slot >= KE_KVA_FIXMAP_ARENA_PAGES)
        return 0;
    if (generation == 0 || generation > KE_TEMP_MAP_TOKEN_GENERATION_MASK)
        return 0;

    return (generation << KE_TEMP_MAP_TOKEN_SLOT_BITS) | slot;
}

static KE_KVA_RANGE_RECORD *
KiKvaFindRecordById(const KE_KVA_RANGE *range)
{
    if (!range || range->RecordId == 0 || range->RecordId > KE_KVA_MAX_RANGES)
        return NULL;
    if (range->Generation == 0)
        return NULL;

    KE_KVA_RANGE_RECORD *record = &gKvaRanges[range->RecordId - 1];
    if (!record->InUse)
        return NULL;
    if (record->Arena != range->Arena)
        return NULL;
    if (record->Generation != range->Generation)
        return NULL;

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(record->Arena);
    if (!arena)
        return NULL;
    HO_VIRTUAL_ADDRESS baseAddress = arena->BaseAddress + record->BasePageIndex * PAGE_4KB;
    if (baseAddress != range->BaseAddress)
        return NULL;
    if (KiKvaRecordUsableBase(arena, record) != range->UsableBase)
        return NULL;
    if (record->TotalPages != range->TotalPages || record->UsablePages != range->UsablePages ||
        record->GuardLowerPages != range->GuardLowerPages || record->GuardUpperPages != range->GuardUpperPages)
    {
        return NULL;
    }

    return record;
}

static HO_STATUS
KiKvaReleaseRecord(KE_KVA_RANGE_RECORD *record, const KE_KVA_ARENA_STATE *arena)
{
    if (!record || !arena)
        return EC_ILLEGAL_ARGUMENT;

    HO_VIRTUAL_ADDRESS usableBase = KiKvaRecordUsableBase(arena, record);
    BOOL teardownStarted = FALSE;
    uint8_t releasedPageState = KE_KVA_PAGE_STATE_FREE;
    if (record->Arena == KE_KVA_ARENA_FIXMAP && record->TotalPages == 1 &&
        KiKvaFixmapSlotRetired(record->BasePageIndex))
        releasedPageState = KE_KVA_PAGE_STATE_RETIRED;

    for (uint64_t pageIdx = 0; pageIdx < record->UsablePages; ++pageIdx)
    {
        HO_VIRTUAL_ADDRESS virtAddr = usableBase + pageIdx * PAGE_4KB;
        KE_PT_MAPPING mapping;
        HO_STATUS status = KePtQueryPage(KeGetKernelAddressSpace(), virtAddr, &mapping);
        if (status != EC_SUCCESS)
        {
            if (teardownStarted)
                HO_KPANIC(status, "KVA teardown failed after partial progress");
            return status;
        }
        if (!mapping.Present)
            continue;
        if (mapping.Level != 1)
        {
            if (teardownStarted)
                HO_KPANIC(EC_NOT_SUPPORTED, "KVA teardown hit non-leaf mapping after partial progress");
            return EC_NOT_SUPPORTED;
        }

        teardownStarted = TRUE;
        status = KePtUnmapPage(KeGetKernelAddressSpace(), virtAddr);
        if (status != EC_SUCCESS)
            HO_KPANIC(status, "KVA teardown failed after unmapping started");

        if (record->OwnsPhysicalBacking)
        {
            status = KePmmFreePages(mapping.PhysicalBase, 1);
            if (status != EC_SUCCESS)
                HO_KPANIC(status, "KVA teardown failed after physical free sequence started");
        }
    }

    for (uint64_t pageIdx = 0; pageIdx < record->TotalPages; ++pageIdx)
        arena->PageStates[record->BasePageIndex + pageIdx] = releasedPageState;

    memset(record, 0, sizeof(*record));
    record->RecordId = (uint32_t)(record - gKvaRanges) + 1;
    return EC_SUCCESS;
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
    memset(gFixmapSlotGenerations, 0, sizeof(gFixmapSlotGenerations));
    gKvaRangeGenerationCounter = 1ULL;

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

    if (guardLowerPages > 0xFFFFFFFFFFFFFFFFULL - usablePages)
        return EC_OUT_OF_RESOURCE;
    uint64_t totalPages = guardLowerPages + usablePages;
    if (guardUpperPages > 0xFFFFFFFFFFFFFFFFULL - totalPages)
        return EC_OUT_OF_RESOURCE;
    totalPages += guardUpperPages;
    if (totalPages == 0 || totalPages > arena->PageCount)
        return EC_OUT_OF_RESOURCE;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_OUT_OF_RESOURCE;
    KeEnterCriticalSection(&criticalSection);

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
        goto cleanup;

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

        uint64_t generation = KiKvaNextRangeGeneration();
        if (generation == 0)
        {
            status = EC_OUT_OF_RESOURCE;
            goto cleanup;
        }

        for (uint64_t offset = 0; offset < totalPages; ++offset)
        {
            uint64_t pageIndex = basePage + offset;
            BOOL isGuard = offset < guardLowerPages || offset >= (guardLowerPages + usablePages);
            arena->PageStates[pageIndex] = isGuard ? KE_KVA_PAGE_STATE_GUARD : KE_KVA_PAGE_STATE_ALLOC;
        }

        memset(record, 0, sizeof(*record));
        record->OwnsPhysicalBacking = ownsPhysicalBacking;
        record->Arena = arenaType;
        record->RecordId = (uint32_t)(record - gKvaRanges) + 1;
        record->Generation = generation;
        record->BasePageIndex = basePage;
        record->TotalPages = totalPages;
        record->UsablePages = usablePages;
        record->GuardLowerPages = guardLowerPages;
        record->GuardUpperPages = guardUpperPages;
        record->InUse = TRUE;

        status = KiKvaFillRangeFromRecord(record, outRange);
        goto cleanup;
    }

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaMapPage(const KE_KVA_RANGE *range, uint64_t usablePageIndex, HO_PHYSICAL_ADDRESS physAddr, uint64_t attributes)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!range)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_SUCCESS;
    KeEnterCriticalSection(&criticalSection);

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordById(range);
    if (!record)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }
    if (usablePageIndex >= record->UsablePages)
    {
        status = EC_ILLEGAL_ARGUMENT;
        goto cleanup;
    }

    HO_VIRTUAL_ADDRESS virtAddr = range->UsableBase + usablePageIndex * PAGE_4KB;
    status = KePtMapPage(KeGetKernelAddressSpace(), virtAddr, physAddr, attributes);

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaMapOwnedPages(const KE_KVA_RANGE *range, uint64_t attributes)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!range)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_SUCCESS;
    uint64_t usablePages = 0;

    KeEnterCriticalSection(&criticalSection);
    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordById(range);
    if (!record || !record->OwnsPhysicalBacking)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }
    usablePages = record->UsablePages;
    KeLeaveCriticalSection(&criticalSection);
    criticalSection.Active = FALSE;

    for (uint64_t pageIdx = 0; pageIdx < usablePages; ++pageIdx)
    {
        HO_PHYSICAL_ADDRESS physAddr = 0;
        status = KePmmAllocPages(1, NULL, &physAddr);
        if (status != EC_SUCCESS)
        {
            HO_STATUS cleanupStatus = KeKvaReleaseRangeHandle(range);
            if (cleanupStatus != EC_SUCCESS)
                return cleanupStatus;
            return status;
        }

        status = KeKvaMapPage(range, pageIdx, physAddr, attributes);
        if (status != EC_SUCCESS)
        {
            (void)KePmmFreePages(physAddr, 1);
            HO_STATUS cleanupStatus = KeKvaReleaseRangeHandle(range);
            if (cleanupStatus != EC_SUCCESS)
                return cleanupStatus;
            return status;
        }
    }

    return EC_SUCCESS;

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaReleaseRangeHandle(const KE_KVA_RANGE *range)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!range)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_SUCCESS;
    KeEnterCriticalSection(&criticalSection);

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordById(range);
    if (!record)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(record->Arena);
    if (!arena)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    status = KiKvaReleaseRecord(record, arena);

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaReleaseRange(HO_VIRTUAL_ADDRESS usableBase)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!HO_IS_ALIGNED(usableBase, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_SUCCESS;
    KeEnterCriticalSection(&criticalSection);

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordByUsableBase(usableBase);
    if (!record)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(record->Arena);
    if (!arena)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    status = KiKvaReleaseRecord(record, arena);

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaQueryRange(HO_VIRTUAL_ADDRESS usableBase, KE_KVA_RANGE *outRange)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outRange)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_SUCCESS;
    KeEnterCriticalSection(&criticalSection);

    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordByUsableBase(usableBase);
    if (!record)
    {
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    status = KiKvaFillRangeFromRecord(record, outRange);

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaQueryArenaInfo(KE_KVA_ARENA_TYPE arenaType, KE_KVA_ARENA_INFO *outInfo)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_SUCCESS;
    KeEnterCriticalSection(&criticalSection);

    KE_KVA_ARENA_STATE *arena = KiKvaArenaState(arenaType);
    if (!arena)
    {
        status = EC_ILLEGAL_ARGUMENT;
        goto cleanup;
    }

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->Arena = arenaType;
    outInfo->BaseAddress = arena->BaseAddress;
    outInfo->EndAddressExclusive = arena->BaseAddress + arena->SizeBytes;
    outInfo->TotalPages = arena->PageCount;
    outInfo->FreePages = KiKvaCountFreePages(arena);
    outInfo->ActiveAllocations = KiKvaCountActiveRanges(arenaType);
    outInfo->OverlapsImportedRegions = KiKvaArenaOverlapsImportedRegions(
        KeGetKernelAddressSpace(), outInfo->BaseAddress, outInfo->EndAddressExclusive);

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaQueryUsageInfo(KE_KVA_USAGE_INFO *outInfo)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->ActiveRangeCount = KiKvaCountAllActiveRanges();
    outInfo->FixmapTotalSlots = gKvaArenas[KE_KVA_ARENA_FIXMAP].PageCount;
    outInfo->FixmapActiveSlots = KiKvaCountActiveRanges(KE_KVA_ARENA_FIXMAP);
    KeLeaveCriticalSection(&criticalSection);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaQueryActiveRanges(KE_KVA_ACTIVE_RANGE_SNAPSHOT *outSnapshot)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outSnapshot)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_SUCCESS;
    KeEnterCriticalSection(&criticalSection);

    memset(outSnapshot, 0, sizeof(*outSnapshot));
    outSnapshot->TotalActiveRangeCount = KiKvaCountAllActiveRanges();

    uint32_t returned = 0;
    for (uint32_t idx = 0; idx < KE_KVA_MAX_RANGES; ++idx)
    {
        if (!gKvaRanges[idx].InUse)
            continue;

        if (returned >= KE_KVA_ACTIVE_RANGE_SNAPSHOT_MAX)
        {
            outSnapshot->Truncated = TRUE;
            break;
        }

        status = KiKvaFillActiveRangeEntry(&gKvaRanges[idx], &outSnapshot->Ranges[returned]);
        if (status != EC_SUCCESS)
            goto cleanup;

        returned++;
    }

    outSnapshot->ReturnedRangeCount = returned;
    outSnapshot->Truncated = outSnapshot->Truncated || (returned < outSnapshot->TotalActiveRangeCount);

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeKvaClassifyAddress(HO_VIRTUAL_ADDRESS virtAddr, KE_KVA_ADDRESS_INFO *outInfo)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_SUCCESS;
    KeEnterCriticalSection(&criticalSection);

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->Kind = KE_KVA_ADDRESS_OUTSIDE;
    outInfo->Arena = KE_KVA_ARENA_MAX;

    KE_KVA_ARENA_TYPE arenaType = KE_KVA_ARENA_MAX;
    KE_KVA_ARENA_STATE *arena = NULL;
    uint64_t pageIndex = 0;
    if (!KiKvaLocateArenaByAddress(virtAddr, &arenaType, &arena, &pageIndex))
        goto cleanup;

    outInfo->InKvaArena = TRUE;
    outInfo->Arena = arenaType;
    outInfo->ArenaBase = arena->BaseAddress;
    outInfo->ArenaEndExclusive = arena->BaseAddress + arena->SizeBytes;
    outInfo->ArenaPageIndex = pageIndex;

    uint8_t pageState = arena->PageStates[pageIndex];
    if (pageState == KE_KVA_PAGE_STATE_FREE)
    {
        outInfo->Kind = KE_KVA_ADDRESS_FREE_HOLE;
        goto cleanup;
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
        status = EC_INVALID_STATE;
        goto cleanup;
    }

    status = KiKvaFillRangeFromRecord(record, &outInfo->Range);
    if (status != EC_SUCCESS)
        goto cleanup;
    outInfo->HasRange = TRUE;

    if (pageState == KE_KVA_PAGE_STATE_GUARD)
    {
        outInfo->Kind = KE_KVA_ADDRESS_GUARD_PAGE;
        goto cleanup;
    }

    if (pageState == KE_KVA_PAGE_STATE_ALLOC)
    {
        outInfo->Kind = KiKvaAddressKindFromArena(arenaType);
        status = outInfo->Kind == KE_KVA_ADDRESS_UNKNOWN ? EC_INVALID_STATE : EC_SUCCESS;
        goto cleanup;
    }

    outInfo->Kind = KE_KVA_ADDRESS_UNKNOWN;
    status = EC_INVALID_STATE;

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
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
KeFixmapAcquire(HO_PHYSICAL_ADDRESS physAddr,
                uint64_t attributes,
                KE_TEMP_PHYS_MAP_HANDLE *outHandle,
                HO_VIRTUAL_ADDRESS *outVirtAddr)
{
    if (!gKvaInitialized)
        return EC_INVALID_STATE;
    if (!outHandle || !outVirtAddr || !HO_IS_ALIGNED(physAddr, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;

    outHandle->Token = 0;
    *outVirtAddr = 0;

    KE_CRITICAL_SECTION criticalSection = {0};
    HO_STATUS status = EC_OUT_OF_RESOURCE;
    KeEnterCriticalSection(&criticalSection);

    KE_KVA_RANGE_RECORD *record = NULL;
    uint32_t slot = 0;
    for (uint32_t idx = 0; idx < KE_KVA_MAX_RANGES; ++idx)
    {
        if (!gKvaRanges[idx].InUse)
        {
            record = &gKvaRanges[idx];
            break;
        }
    }
    if (!record)
        goto cleanup;

    KE_KVA_ARENA_STATE *arena = &gKvaArenas[KE_KVA_ARENA_FIXMAP];
    for (slot = 0; slot < arena->PageCount; ++slot)
    {
        if (arena->PageStates[slot] != KE_KVA_PAGE_STATE_FREE)
            continue;
        if (KiKvaFixmapSlotRetired(slot))
        {
            arena->PageStates[slot] = KE_KVA_PAGE_STATE_RETIRED;
            continue;
        }

        uint64_t generation = KiNextFixmapSlotGeneration(slot);
        if (generation == 0)
        {
            arena->PageStates[slot] = KE_KVA_PAGE_STATE_RETIRED;
            continue;
        }

        arena->PageStates[slot] = KE_KVA_PAGE_STATE_ALLOC;
        memset(record, 0, sizeof(*record));
        record->OwnsPhysicalBacking = FALSE;
        record->Arena = KE_KVA_ARENA_FIXMAP;
        record->RecordId = (uint32_t)(record - gKvaRanges) + 1;
        record->Generation = generation;
        record->BasePageIndex = slot;
        record->TotalPages = 1;
        record->UsablePages = 1;
        record->GuardLowerPages = 0;
        record->GuardUpperPages = 0;
        record->InUse = TRUE;

        KE_KVA_RANGE range;
        status = KiKvaFillRangeFromRecord(record, &range);
        if (status != EC_SUCCESS)
        {
            arena->PageStates[slot] = KE_KVA_PAGE_STATE_FREE;
            memset(record, 0, sizeof(*record));
            record->RecordId = (uint32_t)(record - gKvaRanges) + 1;
            goto cleanup;
        }

        outHandle->Token = KiEncodeTempMapToken(slot, generation);
        if (outHandle->Token == 0)
        {
            arena->PageStates[slot] = KE_KVA_PAGE_STATE_FREE;
            memset(record, 0, sizeof(*record));
            record->RecordId = (uint32_t)(record - gKvaRanges) + 1;
            status = EC_INVALID_STATE;
            goto cleanup;
        }

        status = KePtMapPage(KeGetKernelAddressSpace(), range.UsableBase, physAddr, attributes);
        if (status != EC_SUCCESS)
        {
            outHandle->Token = 0;
            arena->PageStates[slot] = KE_KVA_PAGE_STATE_FREE;
            memset(record, 0, sizeof(*record));
            record->RecordId = (uint32_t)(record - gKvaRanges) + 1;
            goto cleanup;
        }

        *outVirtAddr = range.UsableBase;
        status = EC_SUCCESS;
        goto cleanup;
    }

cleanup:
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeFixmapRelease(KE_TEMP_PHYS_MAP_HANDLE *handle)
{
    return KeTempPhysMapRelease(handle);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeTempPhysMapAcquire(HO_PHYSICAL_ADDRESS physAddr,
                     uint64_t attributes,
                     KE_TEMP_PHYS_MAP_HANDLE *outHandle,
                     HO_VIRTUAL_ADDRESS *outVirtAddr)
{
    if (!outHandle || !outVirtAddr || !HO_IS_ALIGNED(physAddr, PAGE_4KB))
        return EC_ILLEGAL_ARGUMENT;

    outHandle->Token = 0;
    *outVirtAddr = 0;
    return KeFixmapAcquire(physAddr, attributes, outHandle, outVirtAddr);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeTempPhysMapRelease(KE_TEMP_PHYS_MAP_HANDLE *handle)
{
    if (!handle)
        return EC_ILLEGAL_ARGUMENT;

    uint32_t slot = 0;
    uint64_t generation = 0;
    HO_STATUS status = KiDecodeTempMapHandle(handle, &slot, &generation);
    if (status != EC_SUCCESS)
        return status;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    HO_VIRTUAL_ADDRESS usableBase = KE_KVA_FIXMAP_ARENA_BASE + (uint64_t)slot * PAGE_4KB;
    KE_KVA_RANGE_RECORD *record = KiKvaFindRecordByUsableBase(usableBase);
    if (!record || record->Arena != KE_KVA_ARENA_FIXMAP)
    {
        KeLeaveCriticalSection(&criticalSection);
        return EC_INVALID_STATE;
    }
    if (gFixmapSlotGenerations[slot] != generation)
    {
        KeLeaveCriticalSection(&criticalSection);
        return EC_INVALID_STATE;
    }

    status = KiKvaReleaseRecord(record, KiKvaArenaState(record->Arena));
    KeLeaveCriticalSection(&criticalSection);
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

    status = KeKvaReleaseRangeHandle(&stackRange);
    if (status != EC_SUCCESS)
        return status;

    HO_PHYSICAL_ADDRESS testPhys = 0;
    status = KePmmAllocPages(1, NULL, &testPhys);
    if (status != EC_SUCCESS)
        return status;

    // HHDM self-check remains in the diagnostics allowlist on purpose.
    volatile uint64_t *alias = (volatile uint64_t *)(uint64_t)HHDM_PHYS2VIRT(testPhys);
    *alias = 0x6B56414649584D50ULL;

    KE_TEMP_PHYS_MAP_HANDLE fixHandleA = {0};
    KE_TEMP_PHYS_MAP_HANDLE fixHandleB = {0};
    HO_VIRTUAL_ADDRESS fixVirtA = 0;
    HO_VIRTUAL_ADDRESS fixVirtB = 0;
    KE_TEMP_PHYS_MAP_HANDLE tempHandleA = {0};
    KE_TEMP_PHYS_MAP_HANDLE tempHandleB = {0};
    KE_TEMP_PHYS_MAP_HANDLE staleHandle = {0};
    HO_VIRTUAL_ADDRESS tempVirtA = 0;
    HO_VIRTUAL_ADDRESS tempVirtB = 0;
    HO_VIRTUAL_ADDRESS reusedTempVirt = 0;
    HO_VIRTUAL_ADDRESS heapVirt = 0;

    status = KeFixmapAcquire(testPhys, KE_KVA_DEFAULT_PAGE_ATTRS, &fixHandleA, &fixVirtA);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;

    volatile uint64_t *fixAliasA = (volatile uint64_t *)(uint64_t)fixVirtA;
    if (*fixAliasA != *alias)
    {
        status = EC_INVALID_STATE;
        goto cleanup_fixmap_slot_a;
    }

    status = KeFixmapRelease(&fixHandleA);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;
    fixVirtA = 0;

    status = KeFixmapAcquire(testPhys, KE_KVA_DEFAULT_PAGE_ATTRS, &fixHandleB, &fixVirtB);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;

    volatile uint64_t *fixAliasB = (volatile uint64_t *)(uint64_t)fixVirtB;
    if (*fixAliasB != *alias)
    {
        status = EC_INVALID_STATE;
        goto cleanup_fixmap_slot_b;
    }

    status = KeFixmapRelease(&fixHandleB);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;
    fixVirtB = 0;

    status = KeTempPhysMapAcquire(testPhys, KE_KVA_DEFAULT_PAGE_ATTRS, &tempHandleA, &tempVirtA);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;
    reusedTempVirt = tempVirtA;

    volatile uint64_t *tempAliasA = (volatile uint64_t *)(uint64_t)tempVirtA;
    if (*tempAliasA != *alias)
    {
        status = EC_INVALID_STATE;
        goto cleanup_temp_map_a;
    }

    staleHandle = tempHandleA;
    status = KeTempPhysMapRelease(&tempHandleA);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;
    tempVirtA = 0;

    status = KeTempPhysMapAcquire(testPhys, KE_KVA_DEFAULT_PAGE_ATTRS, &tempHandleB, &tempVirtB);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;
    if (tempVirtB != reusedTempVirt)
    {
        status = EC_INVALID_STATE;
        goto cleanup_temp_map_b;
    }

    volatile uint64_t *tempAliasB = (volatile uint64_t *)(uint64_t)tempVirtB;
    if (*tempAliasB != *alias)
    {
        status = EC_INVALID_STATE;
        goto cleanup_temp_map_b;
    }

    HO_STATUS staleStatus = KeTempPhysMapRelease(&staleHandle);
    if (staleStatus != EC_INVALID_STATE)
    {
        status = EC_INVALID_STATE;
        goto cleanup_temp_map_b;
    }
    if (*tempAliasB != *alias)
    {
        status = EC_INVALID_STATE;
        goto cleanup_temp_map_b;
    }

    status = KeTempPhysMapRelease(&tempHandleB);
    if (status != EC_SUCCESS)
        goto cleanup_fixmap_phys;
    tempVirtB = 0;

    staleStatus = KeTempPhysMapRelease(&tempHandleB);
    if (staleStatus != EC_INVALID_STATE)
    {
        status = EC_INVALID_STATE;
        goto cleanup_fixmap_phys;
    }

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
    heapVirt = 0;

    status = KePmmFreePages(testPhys, 1);
    if (status != EC_SUCCESS)
        return status;
    testPhys = 0;

    klog(KLOG_LEVEL_INFO,
         "[KVA] self-test OK: guard pages, fixmap reuse, temp-map ownership, and heap growth verified\n");
    return EC_SUCCESS;

cleanup_heap:
    if (heapVirt != 0)
    {
        HO_STATUS cleanupStatus = KeHeapFreePages(heapVirt);
        if (cleanupStatus == EC_SUCCESS)
        {
            heapVirt = 0;
        }
        else if (status == EC_SUCCESS)
        {
            status = cleanupStatus;
        }
    }
cleanup_temp_map_b:
    if (tempVirtB != 0)
    {
        HO_STATUS cleanupStatus = KeTempPhysMapRelease(&tempHandleB);
        if (cleanupStatus == EC_SUCCESS)
        {
            tempVirtB = 0;
        }
        else
        {
            klog(KLOG_LEVEL_ERROR,
                 "[KVA] self-test cleanup preserved PMM page %p because temp map release failed (%s, %p)\n",
                 (void *)(uint64_t)testPhys, KrGetStatusMessage(cleanupStatus), cleanupStatus);
            if (status == EC_SUCCESS)
                status = cleanupStatus;
        }
    }
cleanup_temp_map_a:
    if (tempVirtA != 0)
    {
        HO_STATUS cleanupStatus = KeTempPhysMapRelease(&tempHandleA);
        if (cleanupStatus == EC_SUCCESS)
        {
            tempVirtA = 0;
        }
        else
        {
            klog(KLOG_LEVEL_ERROR,
                 "[KVA] self-test cleanup preserved PMM page %p because temp map release failed (%s, %p)\n",
                 (void *)(uint64_t)testPhys, KrGetStatusMessage(cleanupStatus), cleanupStatus);
            if (status == EC_SUCCESS)
                status = cleanupStatus;
        }
    }
cleanup_fixmap_slot_b:
    if (fixVirtB != 0)
    {
        HO_STATUS cleanupStatus = KeFixmapRelease(&fixHandleB);
        if (cleanupStatus == EC_SUCCESS)
        {
            fixVirtB = 0;
        }
        else
        {
            klog(KLOG_LEVEL_ERROR,
                 "[KVA] self-test cleanup preserved PMM page %p because fixmap handle B release failed (%s, %p)\n",
                 (void *)(uint64_t)testPhys, KrGetStatusMessage(cleanupStatus), cleanupStatus);
            if (status == EC_SUCCESS)
                status = cleanupStatus;
        }
    }
cleanup_fixmap_slot_a:
    if (fixVirtA != 0)
    {
        HO_STATUS cleanupStatus = KeFixmapRelease(&fixHandleA);
        if (cleanupStatus == EC_SUCCESS)
        {
            fixVirtA = 0;
        }
        else
        {
            klog(KLOG_LEVEL_ERROR,
                 "[KVA] self-test cleanup preserved PMM page %p because fixmap handle A release failed (%s, %p)\n",
                 (void *)(uint64_t)testPhys, KrGetStatusMessage(cleanupStatus), cleanupStatus);
            if (status == EC_SUCCESS)
                status = cleanupStatus;
        }
    }
cleanup_fixmap_phys:
    if (testPhys != 0)
    {
        if (fixVirtA == 0 && fixVirtB == 0 && tempVirtA == 0 && tempVirtB == 0)
        {
            HO_STATUS cleanupStatus = KePmmFreePages(testPhys, 1);
            if (cleanupStatus != EC_SUCCESS && status == EC_SUCCESS)
                status = cleanupStatus;
        }
        else
        {
            klog(KLOG_LEVEL_ERROR,
                 "[KVA] self-test preserved PMM page %p because a fixmap/temp alias is still active\n",
                 (void *)(uint64_t)testPhys);
        }
    }
    return status;
}
