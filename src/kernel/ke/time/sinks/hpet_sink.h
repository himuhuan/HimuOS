/**
 * HimuOperatingSystem
 *
 * File: ke/time/hpet_sink.h
 * Description:
 * Ke Layer - HPET time sink
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ke/sinks/time_sink.h>
#include <drivers/time/hpet_driver.h>

typedef struct KE_HPET_TIME_SINK
{
    KE_TIME_SINK Base;
    HO_VIRTUAL_ADDRESS BaseVirt;
    uint64_t FreqHz;
    BOOL Initialized;
} KE_HPET_TIME_SINK;

/**
 * @brief Initialize the HPET sink.
 * Discover HPET via ACPI and enable it.
 * @param sink Pointer to sink storage.
 * @param acpiRsdpPhys ACPI RSDP physical address.
 * @return EC_SUCCESS if initialized and usable.
 */
HO_STATUS KeHpetTimeSinkInit(KE_HPET_TIME_SINK *sink, HO_PHYSICAL_ADDRESS acpiRsdpPhys);
