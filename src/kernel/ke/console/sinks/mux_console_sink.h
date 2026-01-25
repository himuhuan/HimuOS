/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/mux_console_sink.h
 * Description:
 * Ke Layer - Multiplexed console sink
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include <kernel/ke/sinks/console_sink.h>

#define MAX_MUX_SINKS 3

HO_INTERNAL_STRUCT typedef struct
{
    KE_CONSOLE_SINK Base;
    KE_CONSOLE_SINK *Sinks[MAX_MUX_SINKS];
    size_t SinkCount;
} MUX_CONSOLE_SINK;

HO_KERNEL_API HO_STATUS KeMuxConSinkInit(MUX_CONSOLE_SINK *muxSink);
HO_KERNEL_API HO_STATUS KeMuxConSinkAddSink(MUX_CONSOLE_SINK *muxSink, KE_CONSOLE_SINK *sink);
