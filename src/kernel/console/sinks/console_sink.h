/**
 * HimuOperatingSystem
 *
 * File: console_sink.h
 * Description:
 * Text console sink interface
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "drivers/basic_color.h"

typedef struct
{
    uint8_t CharWidth, CharHeight, Scale, CharPerTab;
    uint16_t GridWidth;
    uint16_t GridHeight;
} CONSOLE_SINK_INFO;

typedef struct CONSOLE_SINK
{
    HO_STATUS (*GetInfo)(void *self, CONSOLE_SINK_INFO *info);
    HO_STATUS (*PutChar)(void *self, uint16_t x, uint16_t y, char c, COLOR32 fg, COLOR32 bg);
    HO_STATUS (*Scroll)(void *self, uint16_t count, COLOR32 fillColor);
} CONSOLE_SINK;