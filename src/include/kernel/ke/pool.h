/**
 * HimuOperatingSystem
 *
 * File: ke/pool.h
 * Description:
 * Ke Layer - Fixed-size kernel object pool (slab-like bootstrap arena).
 * CHARACTERISTICS:
 * 1. Zero Per-Object Overhead: Instead of maintaining external metadata or
 * object headers, this pool time-multiplexes the memory slots. When a slot
 * is free, it is treated as a KE_POOL_FREE_NODE to store the 'Next' pointer.
 * When allocated, the entire slot is handed over to the caller as raw data.
 *
 * 2. Minimum Slot Size: To ensure the 'Next' pointer always fits, the slot
 * size is strictly enforced to be at least sizeof(void*), even if the
 * requested object size is smaller.
 *
 * 3. O(1) Complexity: Both allocation and deallocation are constant-time
 * operations, involving only simple pointer swaps at the head of the list.
 *
 * 4. Safety Warning: Since the free-list pointers reside within the objects
 * themselves, any Use-After-Free (UAF) or buffer overflow by the consumer
 * will directly corrupt the pool's internal linkage, leading to system-wide
 * instability.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

typedef struct KE_POOL_FREE_NODE
{
    struct KE_POOL_FREE_NODE *Next;
} KE_POOL_FREE_NODE;

typedef struct KE_POOL
{
    KE_POOL_FREE_NODE *FreeList;
    size_t SlotSize;
    uint32_t SlotsPerPage;
    uint32_t TotalSlots;
    uint32_t UsedSlots;
    const char *Name;
} KE_POOL;

/**
 * @brief Initialize an object pool.
 * @param pool Pool structure to initialize.
 * @param objectSize Size of each object in bytes.
 * @param initialCapacity Minimum number of slots to pre-allocate.
 * @param name Descriptive name for debugging.
 * @return EC_SUCCESS on success, EC_NOT_ENOUGH_MEMORY if PMM allocation fails.
 */
HO_KERNEL_API HO_STATUS KePoolInit(KE_POOL *pool, size_t objectSize, uint32_t initialCapacity, const char *name);

/**
 * @brief Allocate an object from the pool (zero-initialized).
 * @param pool Pool to allocate from.
 * @return Pointer to allocated object, or NULL if pool and PMM are exhausted.
 */
HO_KERNEL_API void *KePoolAlloc(KE_POOL *pool);

/**
 * @brief Return an object to the pool. NULL is a no-op.
 */
HO_KERNEL_API void KePoolFree(KE_POOL *pool, void *object);
