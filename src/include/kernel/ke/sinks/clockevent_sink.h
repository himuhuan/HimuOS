/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/clockevent_sink.h
 * Description:
 * Ke Layer - Clockevent sink interface
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#define KE_CLOCKEVENT_MAX_CPU                1

#define KE_CLOCKEVENT_CAP_ONE_SHOT_COUNTDOWN (1U << 0)
#define KE_CLOCKEVENT_CAP_TSC_DEADLINE       (1U << 1)

typedef struct KE_CLOCKEVENT_SINK
{
    HO_STATUS (*InitPerCpu)(void *self, uint32_t cpuIndex, uint8_t vectorNumber);
    HO_STATUS (*SetNextEvent)(void *self, uint32_t cpuIndex, uint64_t deltaNs);
    void (*AckInterrupt)(void *self, uint32_t cpuIndex);
    uint64_t (*GetFrequencyHz)(void *self, uint32_t cpuIndex);
    uint32_t (*GetCapabilityFlags)(void *self);
    const char *(*GetName)(void *self);
} KE_CLOCKEVENT_SINK;
