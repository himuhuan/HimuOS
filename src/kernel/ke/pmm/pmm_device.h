/**
 * HimuOperatingSystem
 *
 * File: ke/pmm/pmm_device.h
 * Description:
 * Ke Layer - Physical Memory Manager device (internal header)
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ke/mm.h>

// Page states (2-bit encoding for bitmap backend)
typedef enum KE_PMM_PAGE_STATE
{
    PMM_PAGE_FREE = 0x0,
    PMM_PAGE_ALLOCATED = 0x1,
    PMM_PAGE_RESERVED = 0x2,
} KE_PMM_PAGE_STATE;

// Sink function table
typedef struct KE_PMM_SINK
{
    HO_STATUS (*AllocPages)(void *self,
                            uint64_t count,
                            const KE_PMM_ALLOC_CONSTRAINTS *constraints,
                            HO_PHYSICAL_ADDRESS *outBasePhys);
    HO_STATUS (*FreePages)(void *self, HO_PHYSICAL_ADDRESS basePhys, uint64_t count);
    HO_STATUS (*ReservePages)(void *self, HO_PHYSICAL_ADDRESS basePhys, uint64_t count);
    HO_STATUS (*QueryStats)(void *self, KE_PMM_STATS *outStats);
    HO_STATUS (*CheckInvariants)(void *self);
} KE_PMM_SINK;

// PMM device
typedef struct KE_PMM_DEVICE
{
    KE_PMM_SINK *Sink;
    void *SinkContext;
    BOOL Initialized;
} KE_PMM_DEVICE;
