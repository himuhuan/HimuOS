/**
 * HimuOperatingSystem
 *
 * File: ke/pmm/bitmap_sink.h
 * Description:
 * Ke Layer - Bitmap-based PMM sink (internal header)
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "pmm_device.h"

typedef struct KE_PMM_BITMAP_CONTEXT
{
    uint8_t *Bitmap;
    HO_PHYSICAL_ADDRESS ManagedBasePhys;
    uint64_t TotalManagedPages;
    uint64_t FreePages;
    uint64_t AllocatedPages;
    uint64_t ReservedPages;
} KE_PMM_BITMAP_CONTEXT;

HO_KERNEL_API HO_STATUS KePmmBitmapSinkInit(KE_PMM_BITMAP_CONTEXT *ctx,
                                            uint8_t *bitmap,
                                            HO_PHYSICAL_ADDRESS managedBasePhys,
                                            uint64_t totalManagedPages);

HO_KERNEL_API KE_PMM_SINK *KePmmBitmapSinkGetSink(void);
