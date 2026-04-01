/**
 * HimuOperatingSystem
 *
 * File: ke/mm/allocator.c
 * Description:
 * Ke Layer - Kernel allocator core implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/critical_section.h>
#include <kernel/ke/mm.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

#define KE_ALLOC_SMALL_CLASS_COUNT      7U
#define KE_ALLOC_LARGE_RECORD_CAPACITY  128U
#define KE_ALLOC_SMALL_PAGE_MAGIC       0x414C534DU /* "ALSM" */
#define KE_ALLOC_SMALL_PAGE_RETIRED     0x414C5352U /* "ALSR" */
#define KE_ALLOC_SLOT_STATE_FREE        0xA110C0DEU
#define KE_ALLOC_SLOT_STATE_ALLOCATED   0xA110CA7EU

typedef struct KE_ALLOC_SMALL_PAGE KE_ALLOC_SMALL_PAGE;

typedef struct KE_ALLOC_SMALL_SLOT
{
    struct KE_ALLOC_SMALL_SLOT *NextFree;
    KE_ALLOC_SMALL_PAGE *OwnerPage;
    uint32_t RequestedSize;
    uint32_t State;
} KE_ALLOC_SMALL_SLOT;

typedef struct KE_ALLOC_LARGE_RECORD
{
    BOOL InUse;
    HO_VIRTUAL_ADDRESS UserPointer;
    size_t RequestedSize;
    HO_VIRTUAL_ADDRESS BackingUsableBase;
    uint64_t BackingUsablePages;
} KE_ALLOC_LARGE_RECORD;

struct KE_ALLOC_SMALL_PAGE
{
    uint32_t Magic;
    uint16_t ClassIndex;
    uint16_t Reserved;
    uint32_t SlotStride;
    uint32_t SlotsPerPage;
    uint32_t FreeCount;
    HO_VIRTUAL_ADDRESS BackingUsableBase;
    uint64_t BackingUsablePages;
    KE_ALLOC_SMALL_SLOT *FreeList;
    KE_ALLOC_SMALL_PAGE *Next;
};

static const size_t gAllocatorClassSizes[KE_ALLOC_SMALL_CLASS_COUNT] = {16U, 32U, 64U, 128U, 256U, 512U, 1024U};

static BOOL gAllocatorInitialized = FALSE;
static KE_ALLOCATOR_STATS gAllocatorStats;
static KE_ALLOC_SMALL_PAGE *gAllocatorSmallPages[KE_ALLOC_SMALL_CLASS_COUNT];
static KE_ALLOC_LARGE_RECORD gAllocatorLargeRecords[KE_ALLOC_LARGE_RECORD_CAPACITY];

static uint64_t
KiAllocatorBackingBytes(uint64_t pages)
{
    return pages * PAGE_4KB;
}

static HO_VIRTUAL_ADDRESS
KiAllocatorEndFromLength(HO_VIRTUAL_ADDRESS base, uint64_t length)
{
    const uint64_t maxU64 = ~0ULL;
    if (length > maxU64 - base)
        return maxU64;
    return base + length;
}

static int32_t
KiAllocatorFindSmallClass(size_t size)
{
    for (uint32_t i = 0; i < KE_ALLOC_SMALL_CLASS_COUNT; ++i)
    {
        if (size <= gAllocatorClassSizes[i])
            return (int32_t)i;
    }
    return -1;
}

static void
KiAllocatorCountFailure(void)
{
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);
    gAllocatorStats.FailedAllocationCount++;
    KeLeaveCriticalSection(&criticalSection);
}

static HO_STATUS
KiAllocatorPrepareSmallPage(uint16_t classIndex, KE_ALLOC_SMALL_PAGE **outPage)
{
    if (!outPage || classIndex >= KE_ALLOC_SMALL_CLASS_COUNT)
        return EC_ILLEGAL_ARGUMENT;

    *outPage = NULL;

    HO_VIRTUAL_ADDRESS pageBase = 0;
    HO_STATUS status = KeHeapAllocPages(1, &pageBase);
    if (status != EC_SUCCESS)
        return status;

    KE_KVA_RANGE range;
    status = KeKvaQueryRange(pageBase, &range);
    if (status != EC_SUCCESS)
    {
        HO_STATUS freeStatus = KeHeapFreePages(pageBase);
        HO_KASSERT(freeStatus == EC_SUCCESS, freeStatus);
        return status;
    }

    KE_ALLOC_SMALL_PAGE *page = (KE_ALLOC_SMALL_PAGE *)(uint64_t)pageBase;
    memset(page, 0, sizeof(*page));

    const size_t classSize = gAllocatorClassSizes[classIndex];
    const uint32_t slotStride = (uint32_t)HO_ALIGN_UP(sizeof(KE_ALLOC_SMALL_SLOT) + classSize, 8U);
    const uint32_t headerBytes = (uint32_t)HO_ALIGN_UP(sizeof(KE_ALLOC_SMALL_PAGE), 8U);
    if (headerBytes >= PAGE_4KB || slotStride == 0)
    {
        HO_STATUS freeStatus = KeHeapFreePages(pageBase);
        HO_KASSERT(freeStatus == EC_SUCCESS, freeStatus);
        return EC_INVALID_STATE;
    }

    const uint32_t usableBytes = (uint32_t)(PAGE_4KB - headerBytes);
    const uint32_t slotsPerPage = usableBytes / slotStride;
    if (slotsPerPage == 0)
    {
        HO_STATUS freeStatus = KeHeapFreePages(pageBase);
        HO_KASSERT(freeStatus == EC_SUCCESS, freeStatus);
        return EC_INVALID_STATE;
    }

    page->Magic = KE_ALLOC_SMALL_PAGE_MAGIC;
    page->ClassIndex = classIndex;
    page->SlotStride = slotStride;
    page->SlotsPerPage = slotsPerPage;
    page->FreeCount = 0;
    page->BackingUsableBase = range.UsableBase;
    page->BackingUsablePages = range.UsablePages;
    page->FreeList = NULL;
    page->Next = NULL;

    uint8_t *slotBase = (uint8_t *)(uint64_t)pageBase + headerBytes;
    for (uint32_t i = 0; i < slotsPerPage; ++i)
    {
        KE_ALLOC_SMALL_SLOT *slot = (KE_ALLOC_SMALL_SLOT *)(void *)(slotBase + i * slotStride);
        slot->OwnerPage = page;
        slot->RequestedSize = 0;
        slot->State = KE_ALLOC_SLOT_STATE_FREE;
        slot->NextFree = page->FreeList;
        page->FreeList = slot;
        page->FreeCount++;
    }

    *outPage = page;
    return EC_SUCCESS;
}

static void *
KiAllocatorAllocFromSmallPage(KE_ALLOC_SMALL_PAGE *page, size_t requestedSize, BOOL zeroed)
{
    if (!page || !page->FreeList)
        return NULL;

    KE_ALLOC_SMALL_SLOT *slot = page->FreeList;
    page->FreeList = slot->NextFree;
    HO_KASSERT(page->FreeCount > 0, EC_INVALID_STATE);
    page->FreeCount--;

    slot->NextFree = NULL;
    slot->State = KE_ALLOC_SLOT_STATE_ALLOCATED;
    slot->RequestedSize = requestedSize > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (uint32_t)requestedSize;

    void *userPointer = (void *)((uint8_t *)slot + sizeof(KE_ALLOC_SMALL_SLOT));
    if (zeroed)
    {
        memset(userPointer, 0, gAllocatorClassSizes[page->ClassIndex]);
    }

    return userPointer;
}

static void *
KiAllocatorAllocLarge(size_t requestedSize, BOOL zeroed)
{
    uint64_t pageCount = (uint64_t)HO_ALIGN_UP(requestedSize, PAGE_4KB) / PAGE_4KB;
    if (pageCount == 0)
    {
        KiAllocatorCountFailure();
        return NULL;
    }

    HO_VIRTUAL_ADDRESS base = 0;
    HO_STATUS status = KeHeapAllocPages(pageCount, &base);
    if (status != EC_SUCCESS)
    {
        KiAllocatorCountFailure();
        return NULL;
    }

    KE_KVA_RANGE range;
    status = KeKvaQueryRange(base, &range);
    if (status != EC_SUCCESS)
    {
        HO_STATUS freeStatus = KeHeapFreePages(base);
        HO_KASSERT(freeStatus == EC_SUCCESS, freeStatus);
        KiAllocatorCountFailure();
        return NULL;
    }

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    KE_ALLOC_LARGE_RECORD *record = NULL;
    for (uint32_t i = 0; i < KE_ALLOC_LARGE_RECORD_CAPACITY; ++i)
    {
        if (!gAllocatorLargeRecords[i].InUse)
        {
            record = &gAllocatorLargeRecords[i];
            break;
        }
    }

    if (!record)
    {
        gAllocatorStats.FailedAllocationCount++;
        KeLeaveCriticalSection(&criticalSection);
        HO_STATUS freeStatus = KeHeapFreePages(base);
        HO_KASSERT(freeStatus == EC_SUCCESS, freeStatus);
        return NULL;
    }

    memset(record, 0, sizeof(*record));
    record->InUse = TRUE;
    record->UserPointer = base;
    record->RequestedSize = requestedSize;
    record->BackingUsableBase = range.UsableBase;
    record->BackingUsablePages = range.UsablePages;

    gAllocatorStats.LiveAllocationCount++;
    gAllocatorStats.LiveLargeAllocationCount++;
    gAllocatorStats.BackingBytes += KiAllocatorBackingBytes(range.UsablePages);

    KeLeaveCriticalSection(&criticalSection);

    void *pointer = (void *)(uint64_t)base;
    if (zeroed)
        memset(pointer, 0, requestedSize);
    return pointer;
}

static void *
KiAllocatorAllocSmall(size_t requestedSize, BOOL zeroed)
{
    int32_t classIndex = KiAllocatorFindSmallClass(requestedSize);
    if (classIndex < 0)
        return NULL;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    KE_ALLOC_SMALL_PAGE *page = gAllocatorSmallPages[classIndex];
    while (page && page->FreeCount == 0)
        page = page->Next;

    if (page)
    {
        void *pointer = KiAllocatorAllocFromSmallPage(page, requestedSize, zeroed);
        if (pointer)
        {
            gAllocatorStats.LiveAllocationCount++;
            gAllocatorStats.LiveSmallAllocationCount++;
        }
        KeLeaveCriticalSection(&criticalSection);
        return pointer;
    }

    KeLeaveCriticalSection(&criticalSection);

    KE_ALLOC_SMALL_PAGE *preparedPage = NULL;
    HO_STATUS status = KiAllocatorPrepareSmallPage((uint16_t)classIndex, &preparedPage);
    if (status != EC_SUCCESS)
    {
        KiAllocatorCountFailure();
        return NULL;
    }

    KeEnterCriticalSection(&criticalSection);
    preparedPage->Next = gAllocatorSmallPages[classIndex];
    gAllocatorSmallPages[classIndex] = preparedPage;
    gAllocatorStats.BackingBytes += KiAllocatorBackingBytes(preparedPage->BackingUsablePages);

    void *pointer = KiAllocatorAllocFromSmallPage(preparedPage, requestedSize, zeroed);
    if (pointer)
    {
        gAllocatorStats.LiveAllocationCount++;
        gAllocatorStats.LiveSmallAllocationCount++;
    }
    else
    {
        gAllocatorStats.FailedAllocationCount++;
    }

    KeLeaveCriticalSection(&criticalSection);
    return pointer;
}

static BOOL
KiAllocatorTryFreeLarge(HO_VIRTUAL_ADDRESS pointer)
{
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    for (uint32_t i = 0; i < KE_ALLOC_LARGE_RECORD_CAPACITY; ++i)
    {
        KE_ALLOC_LARGE_RECORD *record = &gAllocatorLargeRecords[i];
        if (!record->InUse || record->UserPointer != pointer)
            continue;

        HO_VIRTUAL_ADDRESS backingBase = record->BackingUsableBase;
        uint64_t backingPages = record->BackingUsablePages;

        record->InUse = FALSE;
        record->UserPointer = 0;
        record->RequestedSize = 0;
        record->BackingUsableBase = 0;
        record->BackingUsablePages = 0;

        HO_KASSERT(gAllocatorStats.LiveAllocationCount > 0, EC_INVALID_STATE);
        HO_KASSERT(gAllocatorStats.LiveLargeAllocationCount > 0, EC_INVALID_STATE);
        gAllocatorStats.LiveAllocationCount--;
        gAllocatorStats.LiveLargeAllocationCount--;

        uint64_t bytes = KiAllocatorBackingBytes(backingPages);
        HO_KASSERT(gAllocatorStats.BackingBytes >= bytes, EC_INVALID_STATE);
        gAllocatorStats.BackingBytes -= bytes;

        KeLeaveCriticalSection(&criticalSection);

        HO_STATUS status = KeHeapFreePages(backingBase);
        HO_KASSERT(status == EC_SUCCESS, status);
        return TRUE;
    }

    KeLeaveCriticalSection(&criticalSection);
    return FALSE;
}

static BOOL
KiAllocatorTryFreeSmall(HO_VIRTUAL_ADDRESS pointer)
{
    HO_VIRTUAL_ADDRESS pageBase = HO_ALIGN_DOWN(pointer, PAGE_4KB);

    HO_VIRTUAL_ADDRESS releaseBase = 0;
    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    KE_KVA_RANGE range;
    if (KeKvaQueryRange(pageBase, &range) != EC_SUCCESS)
    {
        KeLeaveCriticalSection(&criticalSection);
        return FALSE;
    }
    if (range.Arena != KE_KVA_ARENA_HEAP || range.UsableBase != pageBase || range.UsablePages != 1)
    {
        KeLeaveCriticalSection(&criticalSection);
        return FALSE;
    }

    KE_ALLOC_SMALL_PAGE *page = (KE_ALLOC_SMALL_PAGE *)(uint64_t)pageBase;
    if (page->Magic != KE_ALLOC_SMALL_PAGE_MAGIC || page->ClassIndex >= KE_ALLOC_SMALL_CLASS_COUNT)
    {
        KeLeaveCriticalSection(&criticalSection);
        return FALSE;
    }
    if (page->SlotStride == 0 || page->SlotsPerPage == 0)
    {
        KeLeaveCriticalSection(&criticalSection);
        return FALSE;
    }

    const uint64_t headerBytes = HO_ALIGN_UP(sizeof(KE_ALLOC_SMALL_PAGE), 8U);
    if (pointer < pageBase + headerBytes + sizeof(KE_ALLOC_SMALL_SLOT) || pointer >= pageBase + PAGE_4KB)
    {
        KeLeaveCriticalSection(&criticalSection);
        return FALSE;
    }

    const uint64_t relative = pointer - pageBase - headerBytes;
    if (relative < sizeof(KE_ALLOC_SMALL_SLOT))
    {
        KeLeaveCriticalSection(&criticalSection);
        return FALSE;
    }

    const uint64_t slotOffset = relative - sizeof(KE_ALLOC_SMALL_SLOT);
    if ((slotOffset % page->SlotStride) != 0)
    {
        KeLeaveCriticalSection(&criticalSection);
        return FALSE;
    }

    KE_ALLOC_SMALL_SLOT *slot = (KE_ALLOC_SMALL_SLOT *)(uint64_t)(pageBase + headerBytes + slotOffset);
    if (slot->OwnerPage != page || slot->State != KE_ALLOC_SLOT_STATE_ALLOCATED)
    {
        KeLeaveCriticalSection(&criticalSection);
        return FALSE;
    }

    slot->State = KE_ALLOC_SLOT_STATE_FREE;
    slot->RequestedSize = 0;
    slot->NextFree = page->FreeList;
    page->FreeList = slot;
    page->FreeCount++;

    HO_KASSERT(gAllocatorStats.LiveAllocationCount > 0, EC_INVALID_STATE);
    HO_KASSERT(gAllocatorStats.LiveSmallAllocationCount > 0, EC_INVALID_STATE);
    gAllocatorStats.LiveAllocationCount--;
    gAllocatorStats.LiveSmallAllocationCount--;

    if (page->FreeCount == page->SlotsPerPage)
    {
        KE_ALLOC_SMALL_PAGE *prev = NULL;
        KE_ALLOC_SMALL_PAGE *cursor = gAllocatorSmallPages[page->ClassIndex];
        while (cursor && cursor != page)
        {
            prev = cursor;
            cursor = cursor->Next;
        }

        if (cursor == page)
        {
            if (prev)
                prev->Next = page->Next;
            else
                gAllocatorSmallPages[page->ClassIndex] = page->Next;

            uint64_t bytes = KiAllocatorBackingBytes(page->BackingUsablePages);
            HO_KASSERT(gAllocatorStats.BackingBytes >= bytes, EC_INVALID_STATE);
            gAllocatorStats.BackingBytes -= bytes;

            page->Magic = KE_ALLOC_SMALL_PAGE_RETIRED;
            releaseBase = page->BackingUsableBase;
        }
    }

    KeLeaveCriticalSection(&criticalSection);

    if (releaseBase != 0)
    {
        HO_STATUS status = KeHeapFreePages(releaseBase);
        HO_KASSERT(status == EC_SUCCESS, status);
    }

    return TRUE;
}

static HO_STATUS
KiAllocatorDiagnoseLargeLocked(HO_VIRTUAL_ADDRESS virtAddr, KE_ALLOCATOR_ADDRESS_INFO *outInfo)
{
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    for (uint32_t i = 0; i < KE_ALLOC_LARGE_RECORD_CAPACITY; ++i)
    {
        const KE_ALLOC_LARGE_RECORD *record = &gAllocatorLargeRecords[i];
        if (!record->InUse)
            continue;

        HO_VIRTUAL_ADDRESS allocationEnd = KiAllocatorEndFromLength(record->UserPointer, (uint64_t)record->RequestedSize);
        if (virtAddr < record->UserPointer || virtAddr >= allocationEnd)
            continue;

        outInfo->LiveAllocation = TRUE;
        outInfo->Kind = KE_ALLOCATOR_ALLOCATION_LARGE;
        outInfo->AllocationBase = record->UserPointer;
        outInfo->AllocationEndExclusive = allocationEnd;
        outInfo->RequestedSize = (uint64_t)record->RequestedSize;
        outInfo->BackingUsableBase = record->BackingUsableBase;
        outInfo->BackingUsablePages = record->BackingUsablePages;
        outInfo->SmallClassIndex = 0;
        outInfo->SmallClassSize = 0;
        return EC_SUCCESS;
    }

    return EC_SUCCESS;
}

static HO_STATUS
KiAllocatorDiagnoseSmallLocked(HO_VIRTUAL_ADDRESS virtAddr, KE_ALLOCATOR_ADDRESS_INFO *outInfo)
{
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    HO_VIRTUAL_ADDRESS pageBase = HO_ALIGN_DOWN(virtAddr, PAGE_4KB);

    KE_KVA_RANGE range = {0};
    if (KeKvaQueryRange(pageBase, &range) != EC_SUCCESS)
        return EC_SUCCESS;
    if (range.Arena != KE_KVA_ARENA_HEAP || range.UsableBase != pageBase || range.UsablePages != 1)
        return EC_SUCCESS;

    KE_ALLOC_SMALL_PAGE *page = (KE_ALLOC_SMALL_PAGE *)(uint64_t)pageBase;
    if (page->Magic != KE_ALLOC_SMALL_PAGE_MAGIC || page->ClassIndex >= KE_ALLOC_SMALL_CLASS_COUNT)
        return EC_SUCCESS;
    if (page->SlotStride == 0 || page->SlotsPerPage == 0)
        return EC_SUCCESS;

    const uint64_t headerBytes = HO_ALIGN_UP(sizeof(KE_ALLOC_SMALL_PAGE), 8U);
    if (virtAddr < pageBase + headerBytes + sizeof(KE_ALLOC_SMALL_SLOT) || virtAddr >= pageBase + PAGE_4KB)
        return EC_SUCCESS;

    const uint64_t relative = virtAddr - pageBase - headerBytes;
    if (relative < sizeof(KE_ALLOC_SMALL_SLOT))
        return EC_SUCCESS;

    const uint64_t slotOffset = relative - sizeof(KE_ALLOC_SMALL_SLOT);
    const uint64_t slotIndex = slotOffset / page->SlotStride;
    if (slotIndex >= page->SlotsPerPage)
        return EC_SUCCESS;

    const uint64_t slotBaseOffset = slotIndex * page->SlotStride;
    KE_ALLOC_SMALL_SLOT *slot = (KE_ALLOC_SMALL_SLOT *)(uint64_t)(pageBase + headerBytes + slotBaseOffset);
    if (slot->OwnerPage != page || slot->State != KE_ALLOC_SLOT_STATE_ALLOCATED)
        return EC_SUCCESS;

    HO_VIRTUAL_ADDRESS userBase = (HO_VIRTUAL_ADDRESS)(uint64_t)((uint8_t *)slot + sizeof(KE_ALLOC_SMALL_SLOT));
    uint32_t classSize = (uint32_t)gAllocatorClassSizes[page->ClassIndex];
    HO_VIRTUAL_ADDRESS classEnd = userBase + classSize;
    if (virtAddr < userBase || virtAddr >= classEnd)
        return EC_SUCCESS;

    uint32_t requestedSize = slot->RequestedSize;
    if (requestedSize == 0 || requestedSize > classSize)
        requestedSize = classSize;

    HO_VIRTUAL_ADDRESS allocationEnd = userBase + requestedSize;
    if (virtAddr >= allocationEnd)
        return EC_SUCCESS;

    outInfo->LiveAllocation = TRUE;
    outInfo->Kind = KE_ALLOCATOR_ALLOCATION_SMALL;
    outInfo->AllocationBase = userBase;
    outInfo->AllocationEndExclusive = allocationEnd;
    outInfo->RequestedSize = requestedSize;
    outInfo->BackingUsableBase = page->BackingUsableBase;
    outInfo->BackingUsablePages = page->BackingUsablePages;
    outInfo->SmallClassIndex = page->ClassIndex;
    outInfo->SmallClassSize = classSize;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
KeAllocatorInit(void)
{
    if (gAllocatorInitialized)
        return EC_SUCCESS;

    memset(gAllocatorSmallPages, 0, sizeof(gAllocatorSmallPages));
    memset(gAllocatorLargeRecords, 0, sizeof(gAllocatorLargeRecords));
    memset(&gAllocatorStats, 0, sizeof(gAllocatorStats));
    gAllocatorInitialized = TRUE;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
KeAllocatorQueryStats(KE_ALLOCATOR_STATS *outStats)
{
    if (outStats == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!gAllocatorInitialized)
        return EC_INVALID_STATE;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);
    *outStats = gAllocatorStats;
    KeLeaveCriticalSection(&criticalSection);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
KeAllocatorDiagnoseAddress(HO_VIRTUAL_ADDRESS virtAddr, KE_ALLOCATOR_ADDRESS_INFO *outInfo)
{
    if (!outInfo)
        return EC_ILLEGAL_ARGUMENT;

    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->Kind = KE_ALLOCATOR_ALLOCATION_UNKNOWN;

    if (!gAllocatorInitialized)
        return EC_INVALID_STATE;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    HO_STATUS status = KiAllocatorDiagnoseLargeLocked(virtAddr, outInfo);
    if (status == EC_SUCCESS && !outInfo->LiveAllocation)
        status = KiAllocatorDiagnoseSmallLocked(virtAddr, outInfo);

    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_KERNEL_API void *
kmalloc(size_t size)
{
    if (size == 0 || !gAllocatorInitialized)
        return NULL;

    int32_t classIndex = KiAllocatorFindSmallClass(size);
    if (classIndex >= 0)
    {
        void *smallPointer = KiAllocatorAllocSmall(size, FALSE);
        if (smallPointer)
            return smallPointer;
        return NULL;
    }

    return KiAllocatorAllocLarge(size, FALSE);
}

HO_KERNEL_API void *
kzalloc(size_t size)
{
    if (size == 0 || !gAllocatorInitialized)
        return NULL;

    int32_t classIndex = KiAllocatorFindSmallClass(size);
    if (classIndex >= 0)
    {
        void *smallPointer = KiAllocatorAllocSmall(size, TRUE);
        if (smallPointer)
            return smallPointer;
        return NULL;
    }

    return KiAllocatorAllocLarge(size, TRUE);
}

HO_KERNEL_API void
kfree(void *ptr)
{
    if (ptr == NULL)
        return;

    if (!gAllocatorInitialized)
        return;

    HO_VIRTUAL_ADDRESS pointer = (HO_VIRTUAL_ADDRESS)(uint64_t)ptr;
    if (KiAllocatorTryFreeLarge(pointer))
        return;

    (void)KiAllocatorTryFreeSmall(pointer);
}
