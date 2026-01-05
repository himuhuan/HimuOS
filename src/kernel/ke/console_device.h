/**
 * HimuOperatingSystem
 *
 * File: ke/console_device.h
 * Description:
 * Ke Layer - Text console device
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "drivers/basic_color.h"
#include "sinks/console_sink.h"

struct KE_CONSOLE_SINK;

typedef struct KE_CONSOLE_DEVICE
{
    uint16_t CursorX, CursorY; // in CHARACTERS unit
    COLOR32 Foreground, Background;
    KE_CONSOLE_SINK *Sink;
    HO_PRIVATE_FIELD struct {
        uint8_t EscSeqState;
        BOOL AnsiHasCode;
        int AnsiCurrentCode;
    } _ParserState;
} KE_CONSOLE_DEVICE;

HO_KERNEL_API void KeConDevInit(KE_CONSOLE_DEVICE *dev, struct KE_CONSOLE_SINK *sink);

HO_KERNEL_API int KeConDevPutChar(KE_CONSOLE_DEVICE *dev, char ch);

HO_KERNEL_API uint64_t KeConDevPutStr(KE_CONSOLE_DEVICE *dev, const char *str);

HO_KERNEL_API void KeConDevClearScreen(KE_CONSOLE_DEVICE *dev, COLOR32 color);
