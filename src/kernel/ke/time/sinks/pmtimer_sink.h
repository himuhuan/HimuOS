/**
 * HimuOperatingSystem
 *
 * File: ke/time/pmtimer_sink.h
 * Description:
 * Ke Layer - PM Timer time sink
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ke/sinks/time_sink.h>
#include <drivers/time/pmtimer_driver.h>

typedef struct KE_PMTIMER_TIME_SINK
{
    KE_TIME_SINK Base;
    uint16_t Port;
    uint64_t FreqHz;
    uint64_t High;
    uint32_t LastRaw;
    uint8_t BitWidth;
    BOOL Initialized;
} KE_PMTIMER_TIME_SINK;

/**
 * @brief Initialize the PM Timer sink.
 * Discover PM Timer via ACPI FADT and setup the port.
 * @param sink Pointer to sink storage.
 * @param acpiRsdpPhys ACPI RSDP physical address.
 * @return EC_SUCCESS if initialized and usable.
 */
HO_STATUS KePmTimerTimeSinkInit(KE_PMTIMER_TIME_SINK *sink, HO_PHYSICAL_ADDRESS acpiRsdpPhys);
