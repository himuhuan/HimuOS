/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/gfx_console_sink.h
 * Description:
 * Ke Layer - Graphics console sink
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "drivers/video_driver.h"
#include "console_sink.h"
#include <lib/tui/bitmap_font.h>

HO_INTERNAL_STRUCT typedef struct
{
    KE_CONSOLE_SINK Base;
    uint8_t Scale;
    VIDEO_DRIVER *Driver;
    BITMAP_FONT_INFO *Font;
} GFX_CONSOLE_SINK;

HO_KERNEL_API
void KeGfxConSinkInit(GFX_CONSOLE_SINK *sink, VIDEO_DRIVER *driver, BITMAP_FONT_INFO *font, uint8_t scale);
