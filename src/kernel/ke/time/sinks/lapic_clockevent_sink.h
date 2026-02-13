/**
 * HimuOperatingSystem
 *
 * File: ke/time/sinks/lapic_clockevent_sink.h
 * Description:
 * Ke Layer - Local APIC clockevent sink.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ke/sinks/clock_event_sink.h>
#include <drivers/time/lapic_timer_driver.h>

typedef struct KE_LAPIC_CLOCK_EVENT_SINK
{
    KE_CLOCK_EVENT_SINK Base;
    HO_PHYSICAL_ADDRESS BasePhys;
    HO_VIRTUAL_ADDRESS BaseVirt;
    uint64_t TicksPerSec;
    uint32_t DividerValue;
    uint8_t VectorNumber;
    BOOL Initialized;
} KE_LAPIC_CLOCK_EVENT_SINK;

HO_STATUS KeLapicClockEventSinkInit(KE_LAPIC_CLOCK_EVENT_SINK *sink, uint8_t vectorNumber);
HO_STATUS KeLapicClockEventSinkCalibrate(KE_LAPIC_CLOCK_EVENT_SINK *sink, uint64_t windowUs);
void KeLapicClockEventSinkSendEoi(KE_LAPIC_CLOCK_EVENT_SINK *sink);
