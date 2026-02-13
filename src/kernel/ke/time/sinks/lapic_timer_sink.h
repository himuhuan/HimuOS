/**
 * HimuOperatingSystem
 *
 * File: ke/time/lapic_timer_sink.h
 * Description:
 * Ke Layer - Local APIC timer clockevent sink
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ke/sinks/clockevent_sink.h>
#include <kernel/ke/sinks/time_sink.h>

typedef struct KE_LAPIC_TIMER_SINK
{
    KE_CLOCKEVENT_SINK Base;
    HO_PHYSICAL_ADDRESS ApicBasePhys;
    HO_VIRTUAL_ADDRESS ApicBaseVirt;
    uint32_t DividerCode;
    uint64_t FrequencyHzByCpu[KE_CLOCKEVENT_MAX_CPU];
    uint8_t VectorByCpu[KE_CLOCKEVENT_MAX_CPU];
    BOOL PerCpuInitialized[KE_CLOCKEVENT_MAX_CPU];
    BOOL Initialized;
} KE_LAPIC_TIMER_SINK;

HO_STATUS KeLapicTimerSinkInit(KE_LAPIC_TIMER_SINK *sink, HO_PHYSICAL_ADDRESS acpiRsdpPhys, KE_TIME_SINK *refSink);
