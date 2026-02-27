/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/clock_event_sink.h
 * Description:
 * Ke Layer - clockevent sink interface.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

typedef struct KE_CLOCK_EVENT_SINK
{
    HO_STATUS (*SetNextEventNs)(void *self, uint64_t deltaNs);
    uint64_t (*GetMinDeltaNs)(void *self);
    uint64_t (*GetMaxDeltaNs)(void *self);
    const char *(*GetName)(void *self);
} KE_CLOCK_EVENT_SINK;
