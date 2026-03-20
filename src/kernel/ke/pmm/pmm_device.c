/**
 * HimuOperatingSystem
 *
 * File: ke/pmm/pmm_device.c
 * Description:
 * Ke Layer - PMM device facade implementation
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "pmm_device.h"
#include "bitmap_sink.h"

#include <kernel/ke/mm.h>

// Global PMM device and bitmap context (used by pmm_boot_init.c)
KE_PMM_DEVICE gPmmDevice = {.Sink = (void *)0, .SinkContext = (void *)0, .Initialized = FALSE};
KE_PMM_BITMAP_CONTEXT gBitmapCtx;

HO_KERNEL_API HO_NODISCARD HO_STATUS
KePmmAllocPages(uint64_t count, const KE_PMM_ALLOC_CONSTRAINTS *constraints, HO_PHYSICAL_ADDRESS *outBasePhys)
{
    if (!gPmmDevice.Initialized)
        return EC_INVALID_STATE;

    HO_STATUS status = gPmmDevice.Sink->AllocPages(gPmmDevice.SinkContext, count, constraints, outBasePhys);
    return status;
}

HO_KERNEL_API HO_STATUS
KePmmFreePages(HO_PHYSICAL_ADDRESS basePhys, uint64_t count)
{
    if (!gPmmDevice.Initialized)
        return EC_INVALID_STATE;
    HO_STATUS status = gPmmDevice.Sink->FreePages(gPmmDevice.SinkContext, basePhys, count);
    return status;
}

HO_KERNEL_API HO_STATUS
KePmmReservePages(HO_PHYSICAL_ADDRESS basePhys, uint64_t count)
{
    if (!gPmmDevice.Initialized)
        return EC_INVALID_STATE;

    HO_STATUS status = gPmmDevice.Sink->ReservePages(gPmmDevice.SinkContext, basePhys, count);
    return status;
}

HO_KERNEL_API HO_STATUS
KePmmQueryStats(KE_PMM_STATS *outStats)
{
    if (!gPmmDevice.Initialized)
        return EC_INVALID_STATE;

    HO_STATUS status = gPmmDevice.Sink->QueryStats(gPmmDevice.SinkContext, outStats);
    return status;
}

HO_KERNEL_API HO_STATUS
KePmmCheckInvariants(void)
{
    if (!gPmmDevice.Initialized)
        return EC_INVALID_STATE;

    HO_STATUS status = gPmmDevice.Sink->CheckInvariants(gPmmDevice.SinkContext);
    return status;
}
