/**
 * HimuOperatingSystem
 *
 * File: ke/mm/pool.c
 * Description:
 * Ke Layer - Fixed-size kernel object pool implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/pool.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/mm.h>
#include <kernel/hodefs.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

typedef struct KE_POOL_PREPARED_PAGE
{
    HO_VIRTUAL_ADDRESS BaseVirt;
    KE_POOL_FREE_NODE *Head;
    KE_POOL_FREE_NODE *Tail;
    uint32_t SlotCount;
} KE_POOL_PREPARED_PAGE;

static HO_STATUS
KiPoolPrepareOnePage(size_t slotSize, KE_POOL_PREPARED_PAGE *page)
{
    page->BaseVirt = 0;
    page->Head = NULL;
    page->Tail = NULL;
    page->SlotCount = 0;

    // Pool backing now comes from the KVA heap foundation instead of directly
    // from PMM, so KeKvaInit() must have completed before any pool can grow.
    HO_STATUS status = KeHeapAllocPages(1, &page->BaseVirt);
    if (status != EC_SUCCESS)
        return status;

    // The first bytes of the page are reserved for the KE_POOL_PAGE_NODE
    // header so we can track every backing page for later destroy.
    uint8_t *base = (uint8_t *)(uint64_t)page->BaseVirt;
    size_t headerSize = HO_ALIGN_UP(sizeof(KE_POOL_PAGE_NODE), slotSize);
    if (headerSize >= PAGE_4KB)
    {
        HO_STATUS freeStatus = KeHeapFreePages(page->BaseVirt);
        HO_KASSERT(freeStatus == EC_SUCCESS, freeStatus);
        return EC_ILLEGAL_ARGUMENT;
    }
    uint32_t usableSlots = (uint32_t)((PAGE_4KB - headerSize) / slotSize);

    uint8_t *slotBase = base + headerSize;
    for (uint32_t i = 0; i < usableSlots; i++)
    {
        KE_POOL_FREE_NODE *node = (KE_POOL_FREE_NODE *)(slotBase + i * slotSize);
        node->Next = page->Head;
        page->Head = node;
        if (page->Tail == NULL)
            page->Tail = node;
        page->SlotCount++;
    }

    return EC_SUCCESS;
}

static void
KiPoolPublishPreparedPage(KE_POOL *pool, KE_POOL_PREPARED_PAGE *page)
{
    KE_CRITICAL_SECTION criticalSection = {0};

    HO_KASSERT(page->Head != NULL, EC_INVALID_STATE);
    HO_KASSERT(page->Tail != NULL, EC_INVALID_STATE);
    HO_KASSERT(page->SlotCount != 0, EC_INVALID_STATE);

    KE_POOL_PAGE_NODE *pageNode = (KE_POOL_PAGE_NODE *)(uint64_t)page->BaseVirt;

    KeEnterCriticalSection(&criticalSection);
    page->Tail->Next = pool->FreeList;
    pool->FreeList = page->Head;
    pool->TotalSlots += page->SlotCount;
    pageNode->Next = pool->PageList;
    pool->PageList = pageNode;
    pool->PageCount++;
    KeLeaveCriticalSection(&criticalSection);
}

static KE_POOL_FREE_NODE *
KiPoolTryPopNode(KE_POOL *pool)
{
    KE_CRITICAL_SECTION criticalSection = {0};
    KE_POOL_FREE_NODE *node;

    KeEnterCriticalSection(&criticalSection);

    if (pool->Magic != KE_POOL_MAGIC_ALIVE)
    {
        KeLeaveCriticalSection(&criticalSection);
        return NULL;
    }

    node = pool->FreeList;
    if (node != NULL)
    {
        pool->FreeList = node->Next;
        pool->UsedSlots++;
        HO_KASSERT(pool->UsedSlots <= pool->TotalSlots, EC_INVALID_STATE);
        if (pool->UsedSlots > pool->PeakUsedSlots)
            pool->PeakUsedSlots = pool->UsedSlots;
    }

    KeLeaveCriticalSection(&criticalSection);
    return node;
}

static KE_POOL_FREE_NODE *
KiPoolPublishPreparedPageAndPop(KE_POOL *pool, KE_POOL_PREPARED_PAGE *page)
{
    KE_CRITICAL_SECTION criticalSection = {0};
    KE_POOL_FREE_NODE *node;

    HO_KASSERT(page->Head != NULL, EC_INVALID_STATE);
    HO_KASSERT(page->Tail != NULL, EC_INVALID_STATE);
    HO_KASSERT(page->SlotCount != 0, EC_INVALID_STATE);

    KE_POOL_PAGE_NODE *pageNode = (KE_POOL_PAGE_NODE *)(uint64_t)page->BaseVirt;

    KeEnterCriticalSection(&criticalSection);

    if (pool->Magic != KE_POOL_MAGIC_ALIVE)
    {
        KeLeaveCriticalSection(&criticalSection);
        // Pool was destroyed while we were preparing a page; free the orphan.
        HO_STATUS orphanStatus = KeHeapFreePages(page->BaseVirt);
        HO_KASSERT(orphanStatus == EC_SUCCESS, orphanStatus);
        return NULL;
    }

    page->Tail->Next = pool->FreeList;
    pool->FreeList = page->Head;
    pool->TotalSlots += page->SlotCount;
    pageNode->Next = pool->PageList;
    pool->PageList = pageNode;
    pool->PageCount++;

    node = pool->FreeList;
    HO_KASSERT(node != NULL, EC_INVALID_STATE);

    pool->FreeList = node->Next;
    pool->UsedSlots++;
    HO_KASSERT(pool->UsedSlots <= pool->TotalSlots, EC_INVALID_STATE);
    if (pool->UsedSlots > pool->PeakUsedSlots)
        pool->PeakUsedSlots = pool->UsedSlots;

    KeLeaveCriticalSection(&criticalSection);
    return node;
}

HO_KERNEL_API HO_STATUS
KePoolInit(KE_POOL *pool, size_t objectSize, uint32_t initialCapacity, const char *name)
{
    // Poison Magic early so callers see a deterministic non-alive state
    // even if we return before reaching the field-init block below.
    pool->Magic = 0;

    size_t slotSize = HO_ALIGN_UP(objectSize, 8);
    if (slotSize < sizeof(KE_POOL_FREE_NODE))
        slotSize = sizeof(KE_POOL_FREE_NODE);

    size_t headerSize = HO_ALIGN_UP(sizeof(KE_POOL_PAGE_NODE), slotSize);
    if (headerSize >= PAGE_4KB)
    {
        klog(KLOG_LEVEL_ERROR, "[POOL] slot size %lu exceeds page capacity\n", (unsigned long)slotSize);
        return EC_ILLEGAL_ARGUMENT;
    }
    uint32_t slotsPerPage = (uint32_t)((PAGE_4KB - headerSize) / slotSize);

    pool->SlotSize = slotSize;
    pool->SlotsPerPage = slotsPerPage;
    pool->FreeList = NULL;
    pool->PageList = NULL;
    pool->TotalSlots = 0;
    pool->UsedSlots = 0;
    pool->PeakUsedSlots = 0;
    pool->FailedGrows = 0;
    pool->PageCount = 0;
    pool->Name = name;
    pool->Magic = 0; // not yet alive; set after pages are attached

    if (pool->SlotsPerPage == 0)
    {
        klog(KLOG_LEVEL_ERROR, "[POOL] slot size %lu exceeds page size\n", (unsigned long)slotSize);
        return EC_ILLEGAL_ARGUMENT;
    }

    uint32_t neededPages = (initialCapacity + pool->SlotsPerPage - 1) / pool->SlotsPerPage;
    if (neededPages == 0)
        neededPages = 1;

    for (uint32_t i = 0; i < neededPages; i++)
    {
        KE_POOL_PREPARED_PAGE page;
        HO_STATUS status = KiPoolPrepareOnePage(slotSize, &page);
        if (status != EC_SUCCESS)
        {
            // Roll back any pages already acquired during this init.
            KE_POOL_PAGE_NODE *cur = pool->PageList;
            while (cur != NULL)
            {
                KE_POOL_PAGE_NODE *next = cur->Next;
                HO_STATUS freeRbStatus = KeHeapFreePages((HO_VIRTUAL_ADDRESS)(uint64_t)cur);
                HO_KASSERT(freeRbStatus == EC_SUCCESS, freeRbStatus);
                cur = next;
            }
            pool->FreeList = NULL;
            pool->PageList = NULL;
            pool->TotalSlots = 0;
            pool->PageCount = 0;
            pool->Magic = 0;
            return status;
        }

        KiPoolPublishPreparedPage(pool, &page);
    }

    // All pages attached; the pool is now ready for concurrent use.
    pool->Magic = KE_POOL_MAGIC_ALIVE;

    klog(KLOG_LEVEL_INFO, "[POOL] \"%s\" ready: slotSize=%lu slots=%u pages=%u\n", name, (unsigned long)slotSize,
         pool->TotalSlots, neededPages);
    return EC_SUCCESS;
}

HO_KERNEL_API void *
KePoolAlloc(KE_POOL *pool)
{
    if (pool->Magic != KE_POOL_MAGIC_ALIVE)
        return NULL;

    KE_POOL_FREE_NODE *node = KiPoolTryPopNode(pool);

    if (node == NULL)
    {
        // Re-check: if the pool was destroyed between the outer fast-path
        // and the pop, do not attempt growth on a dead pool.
        size_t slotSize = pool->SlotSize;
        if (pool->Magic != KE_POOL_MAGIC_ALIVE || slotSize == 0)
            return NULL;

        KE_POOL_PREPARED_PAGE page;
        if (KiPoolPrepareOnePage(slotSize, &page) != EC_SUCCESS)
        {
            KE_CRITICAL_SECTION cs = {0};
            KeEnterCriticalSection(&cs);
            if (pool->Magic == KE_POOL_MAGIC_ALIVE)
                pool->FailedGrows++;
            KeLeaveCriticalSection(&cs);
            return NULL;
        }

        node = KiPoolPublishPreparedPageAndPop(pool, &page);
        if (node == NULL)
            return NULL; // pool was destroyed concurrently
    }

    memset(node, 0, pool->SlotSize);
    return (void *)node;
}

HO_KERNEL_API void
KePoolFree(KE_POOL *pool, void *object)
{
    if (!object)
        return;

    KE_CRITICAL_SECTION criticalSection = {0};
    KE_POOL_FREE_NODE *node = (KE_POOL_FREE_NODE *)object;

    KeEnterCriticalSection(&criticalSection);
    HO_KASSERT(pool->UsedSlots != 0, EC_INVALID_STATE);
    node->Next = pool->FreeList;
    pool->FreeList = node;
    pool->UsedSlots--;
    KeLeaveCriticalSection(&criticalSection);
}

HO_KERNEL_API HO_STATUS
KePoolDestroy(KE_POOL *pool)
{
    KE_CRITICAL_SECTION criticalSection = {0};
    KE_POOL_PAGE_NODE *pageList;
    const char *name;

    KeEnterCriticalSection(&criticalSection);

    if (pool->Magic != KE_POOL_MAGIC_ALIVE)
    {
        KeLeaveCriticalSection(&criticalSection);
        klog(KLOG_LEVEL_ERROR, "[POOL] KePoolDestroy: pool is not in an initialized state\n");
        return EC_INVALID_STATE;
    }

    if (pool->UsedSlots != 0)
    {
        KeLeaveCriticalSection(&criticalSection);
        klog(KLOG_LEVEL_ERROR, "[POOL] KePoolDestroy(\"%s\"): UsedSlots=%u, cannot destroy with live objects\n",
             pool->Name ? pool->Name : "?", pool->UsedSlots);
        return EC_INVALID_STATE;
    }

    // Atomically mark as dead and detach page list so concurrent
    // allocators see DEAD before any backing page is freed.
    pool->Magic = KE_POOL_MAGIC_DEAD;
    pageList = pool->PageList;
    name = pool->Name;

    // Poison all fields under the lock.
    pool->FreeList = NULL;
    pool->PageList = NULL;
    pool->TotalSlots = 0;
    pool->UsedSlots = 0;
    pool->PeakUsedSlots = 0;
    pool->FailedGrows = 0;
    pool->PageCount = 0;
    pool->SlotsPerPage = 0;
    pool->SlotSize = 0;
    pool->Name = NULL;

    KeLeaveCriticalSection(&criticalSection);

    // Release all backing pages outside the critical section.
    // Safe because the page list is now private (detached above).
    KE_POOL_PAGE_NODE *cur = pageList;
    while (cur != NULL)
    {
        KE_POOL_PAGE_NODE *next = cur->Next;
        HO_STATUS freeStatus = KeHeapFreePages((HO_VIRTUAL_ADDRESS)(uint64_t)cur);
        HO_KASSERT(freeStatus == EC_SUCCESS, freeStatus);
        cur = next;
    }

    klog(KLOG_LEVEL_INFO, "[POOL] \"%s\" destroyed\n", name ? name : "?");
    return EC_SUCCESS;
}

HO_KERNEL_API void
KePoolQueryStats(const KE_POOL *pool, KE_POOL_STATS *stats)
{
    KE_CRITICAL_SECTION criticalSection = {0};

    KeEnterCriticalSection(&criticalSection);
    stats->TotalSlots = pool->TotalSlots;
    stats->UsedSlots = pool->UsedSlots;
    stats->FreeSlots = pool->TotalSlots - pool->UsedSlots;
    stats->PageCount = pool->PageCount;
    stats->PeakUsedSlots = pool->PeakUsedSlots;
    stats->FailedGrowCount = pool->FailedGrows;
    KeLeaveCriticalSection(&criticalSection);
}
