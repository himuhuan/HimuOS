/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/serial_console_sink.h
 * Description:
 * Ke Layer - Serial console sink
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "console_sink.h"
#include <drivers/serial.h>

HO_INTERNAL_STRUCT typedef struct
{
    KE_CONSOLE_SINK Base;
    uint16_t Port;
    uint16_t CurrentRow; // for scroll tracking
} SERIAL_CONSOLE_SINK;

HO_KERNEL_API void KeSerialConSinkInit(SERIAL_CONSOLE_SINK *sink, uint16_t port);
