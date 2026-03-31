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
KiPoolPrepareOnePage(KE_POOL *pool, KE_POOL_PREPARED_PAGE *page)
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

    uint8_t *base = (uint8_t *)(uint64_t)page->BaseVirt;

    for (uint32_t i = 0; i < pool->SlotsPerPage; i++)
    {
        KE_POOL_FREE_NODE *node = (KE_POOL_FREE_NODE *)(base + i * pool->SlotSize);
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

    KeEnterCriticalSection(&criticalSection);
    page->Tail->Next = pool->FreeList;
    pool->FreeList = page->Head;
    pool->TotalSlots += page->SlotCount;
    KeLeaveCriticalSection(&criticalSection);
}

static KE_POOL_FREE_NODE *
KiPoolTryPopNode(KE_POOL *pool)
{
    KE_CRITICAL_SECTION criticalSection = {0};
    KE_POOL_FREE_NODE *node;

    KeEnterCriticalSection(&criticalSection);

    node = pool->FreeList;
    if (node != NULL)
    {
        pool->FreeList = node->Next;
        pool->UsedSlots++;
        HO_KASSERT(pool->UsedSlots <= pool->TotalSlots, EC_INVALID_STATE);
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

    KeEnterCriticalSection(&criticalSection);

    page->Tail->Next = pool->FreeList;
    pool->FreeList = page->Head;
    pool->TotalSlots += page->SlotCount;

    node = pool->FreeList;
    HO_KASSERT(node != NULL, EC_INVALID_STATE);

    pool->FreeList = node->Next;
    pool->UsedSlots++;
    HO_KASSERT(pool->UsedSlots <= pool->TotalSlots, EC_INVALID_STATE);

    KeLeaveCriticalSection(&criticalSection);
    return node;
}

HO_KERNEL_API HO_STATUS
KePoolInit(KE_POOL *pool, size_t objectSize, uint32_t initialCapacity, const char *name)
{
    size_t slotSize = HO_ALIGN_UP(objectSize, 8);
    if (slotSize < sizeof(KE_POOL_FREE_NODE))
        slotSize = sizeof(KE_POOL_FREE_NODE);

    pool->SlotSize = slotSize;
    pool->SlotsPerPage = (uint32_t)(PAGE_4KB / slotSize);
    pool->FreeList = NULL;
    pool->TotalSlots = 0;
    pool->UsedSlots = 0;
    pool->Name = name;

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
        HO_STATUS status = KiPoolPrepareOnePage(pool, &page);
        if (status != EC_SUCCESS)
            return status;

        KiPoolPublishPreparedPage(pool, &page);
    }

    klog(KLOG_LEVEL_INFO, "[POOL] \"%s\" ready: slotSize=%lu slots=%u pages=%u\n", name, (unsigned long)slotSize,
         pool->TotalSlots, neededPages);
    return EC_SUCCESS;
}

HO_KERNEL_API void *
KePoolAlloc(KE_POOL *pool)
{
    KE_POOL_FREE_NODE *node = KiPoolTryPopNode(pool);

    if (node == NULL)
    {
        KE_POOL_PREPARED_PAGE page;
        if (KiPoolPrepareOnePage(pool, &page) != EC_SUCCESS)
            return NULL;

        node = KiPoolPublishPreparedPageAndPop(pool, &page);
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
