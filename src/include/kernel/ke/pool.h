/**
 * HimuOperatingSystem
 *
 * File: ke/pool.h
 * Description:
 * Ke Layer - Fixed-size kernel object pool (slab-like bootstrap arena)
 * backed by KVA heap-foundation pages.
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

/**
 * @brief Intrusive singly-linked list node tracking each backing page
 *        acquired by a pool. Stored at the very beginning of the page.
 *
 * Because every slot is at least sizeof(void*)-aligned and the first
 * slot starts after this header, the header does not overlap any slot.
 * The header occupies sizeof(void*) bytes (one pointer) which is
 * deducted from the usable slot area of the page.
 */
typedef struct KE_POOL_PAGE_NODE
{
    struct KE_POOL_PAGE_NODE *Next;
} KE_POOL_PAGE_NODE;

#define KE_POOL_MAGIC_ALIVE 0x504F4F4CU /* "POOL" */
#define KE_POOL_MAGIC_DEAD  0x44454144U /* "DEAD" */

typedef struct KE_POOL
{
    uint32_t Magic;
    KE_POOL_FREE_NODE *FreeList;
    KE_POOL_PAGE_NODE *PageList;
    size_t SlotSize;
    uint32_t SlotsPerPage;
    uint32_t TotalSlots;
    uint32_t UsedSlots;
    uint32_t PeakUsedSlots;
    uint32_t FailedGrows;
    uint32_t PageCount;
    const char *Name;
} KE_POOL;

typedef struct KE_POOL_STATS
{
    uint32_t TotalSlots;
    uint32_t UsedSlots;
    uint32_t FreeSlots;
    uint32_t PageCount;
    uint32_t PeakUsedSlots;
    uint32_t FailedGrowCount;
} KE_POOL_STATS;

/**
 * @brief Initialize an object pool.
 * @param pool Pool structure to initialize.
 * @param objectSize Size of each object in bytes.
 * @param initialCapacity Minimum number of slots to pre-allocate.
 * @param name Descriptive name for debugging.
 * @return EC_SUCCESS on success, or an error returned while growing the pool
 * from the KVA heap foundation.
 *
 * Backing storage is obtained page-by-page through KeHeapAllocPages(), not by
 * calling PMM directly.
 *
 * The kernel heap foundation must already be initialized (via KeKvaInit())
 * before this call can succeed.
 *
 * CONCURRENCY: The caller must ensure no other thread is concurrently
 * calling KePoolInit, KePoolAlloc, KePoolFree, or KePoolDestroy on the
 * same pool structure.  Typically called once during single-threaded boot.
 */
HO_KERNEL_API HO_STATUS KePoolInit(KE_POOL *pool, size_t objectSize, uint32_t initialCapacity, const char *name);

/**
 * @brief Allocate an object from the pool (zero-initialized).
 * @param pool Pool to allocate from.
 * @return Pointer to allocated object, or NULL if the freelist is empty and
 * one-page growth from the KVA heap foundation fails.
 *
 * Successful allocations always come from pool-owned KVA-backed pages. The
 * returned object remains pool-owned and must be returned with KePoolFree().
 */
HO_KERNEL_API void *KePoolAlloc(KE_POOL *pool);

/**
 * @brief Return an object to the pool. NULL is a no-op.
 *
 * This only recycles the slot into the freelist; it does not release the
 * underlying backing page back to the KVA heap foundation.
 */
HO_KERNEL_API void KePoolFree(KE_POOL *pool, void *object);

/**
 * @brief Destroy an object pool, releasing all backing pages to the KVA
 *        heap foundation.
 * @param pool Pool to destroy. Must have been successfully initialized
 *             via KePoolInit() and must have UsedSlots == 0.
 * @return EC_SUCCESS on success, EC_INVALID_STATE if the pool still has
 *         outstanding allocations or is not in an initialized state.
 *
 * After successful destroy the pool must be re-initialized with
 * KePoolInit() before any further KePoolAlloc() calls.
 *
 * CONCURRENCY: The caller must ensure the pool is fully quiesced —
 * no concurrent KePoolAlloc, KePoolFree, or KePoolInit calls may be
 * in flight on the same pool.  Concurrent KePoolAlloc attempts that
 * are already past the fast-path Magic check will safely observe the
 * DEAD state inside the critical section and return NULL.
 */
HO_KERNEL_API HO_STATUS KePoolDestroy(KE_POOL *pool);

/**
 * @brief Take a consistent snapshot of pool statistics.
 * @param pool Pool to query. Must be alive (initialized, not destroyed).
 * @param stats Output structure filled with the snapshot.
 */
HO_KERNEL_API void KePoolQueryStats(const KE_POOL *pool, KE_POOL_STATS *stats);
