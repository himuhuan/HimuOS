/**
 * HimuOperatingSystem
 *
 * File: mux_console_sink.h
 * Description:
 * Multiplexed console sink
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "console_sink.h"

#define MAX_MUX_SINKS 3

HO_INTERNAL_STRUCT typedef struct
{
    CONSOLE_SINK Base;
    CONSOLE_SINK *Sinks[MAX_MUX_SINKS];
    size_t SinkCount;
} MUX_CONSOLE_SINK;

HO_KERNEL_API HO_STATUS MuxConSinkInit(MUX_CONSOLE_SINK *muxSink);
HO_KERNEL_API HO_STATUS MuxConSinkAddSink(MUX_CONSOLE_SINK *muxSink, CONSOLE_SINK *sink);
