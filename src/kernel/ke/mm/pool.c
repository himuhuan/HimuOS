/**
 * HimuOperatingSystem
 *
 * File: ke/mm/pool.c
 * Description:
 * Ke Layer - Fixed-size kernel object pool implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/pool.h>
#include <kernel/ke/mm.h>
#include <kernel/hodefs.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

static HO_STATUS
KiPoolExpandOnePage(KE_POOL *pool)
{
    HO_PHYSICAL_ADDRESS phys;
    HO_STATUS status = KePmmAllocPages(1, (void *)0, &phys);
    if (status != EC_SUCCESS)
        return status;

    uint8_t *base = (uint8_t *)HHDM_PHYS2VIRT(phys);

    for (uint32_t i = 0; i < pool->SlotsPerPage; i++)
    {
        KE_POOL_FREE_NODE *node = (KE_POOL_FREE_NODE *)(base + i * pool->SlotSize);
        node->Next = pool->FreeList;
        pool->FreeList = node;
        pool->TotalSlots++;
    }

    return EC_SUCCESS;
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
        HO_STATUS status = KiPoolExpandOnePage(pool);
        if (status != EC_SUCCESS)
            return status;
    }

    klog(KLOG_LEVEL_INFO, "[POOL] \"%s\" ready: slotSize=%lu slots=%u pages=%u\n", name, (unsigned long)slotSize,
         pool->TotalSlots, neededPages);
    return EC_SUCCESS;
}

HO_KERNEL_API void *
KePoolAlloc(KE_POOL *pool)
{
    if (pool->FreeList == NULL)
    {
        if (KiPoolExpandOnePage(pool) != EC_SUCCESS)
            return NULL;
    }

    KE_POOL_FREE_NODE *node = pool->FreeList;
    pool->FreeList = node->Next;
    pool->UsedSlots++;

    memset(node, 0, pool->SlotSize);
    return (void *)node;
}

HO_KERNEL_API void
KePoolFree(KE_POOL *pool, void *object)
{
    if (!object)
        return;

    KE_POOL_FREE_NODE *node = (KE_POOL_FREE_NODE *)object;
    node->Next = pool->FreeList;
    pool->FreeList = node;
    pool->UsedSlots--;
}
