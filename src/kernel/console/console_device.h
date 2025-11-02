/**
 * HimuOperatingSystem
 *
 * File: console_device.h
 * Description:
 * Text console device
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "drivers/basic_color.h"
#include "sinks/console_sink.h"

struct CONSOLE_SINK;

typedef struct CONSOLE_DEVICE
{
    uint16_t CursorX, CursorY; // in CHARACTERS unit
    COLOR32 Foreground, Background;
    CONSOLE_SINK *Sink;
    HO_PRIVATE_FIELD struct {
        uint8_t EscSeqState;
        BOOL AnsiHasCode;
        int AnsiCurrentCode;
    } _ParserState;
} CONSOLE_DEVICE;

HO_KERNEL_API void ConDevInit(CONSOLE_DEVICE *dev, struct CONSOLE_SINK *sink);

HO_KERNEL_API int ConDevPutChar(CONSOLE_DEVICE *dev, char ch);

HO_KERNEL_API uint64_t ConDevPutStr(CONSOLE_DEVICE *dev, const char *str);

HO_KERNEL_API void ConDevClearScreen(CONSOLE_DEVICE *dev, COLOR32 color);