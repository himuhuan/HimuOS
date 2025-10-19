/**
 * HimuOperatingSystem
 *
 * File: text_render.h
 * Description:
 * Text rendering functions for TUI (Text User Interface).
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "bitmap_font.h"
#include "drivers/video_device.h"

typedef struct
{
    uint16_t Char;
    uint16_t Scale; // Scaling factor (1 = normal size)
    uint32_t X;
    uint32_t Y;
    uint32_t TextColor;
    uint32_t BackgroundColor;
} TR_RENDER_CHAR_PARAMS;

typedef struct
{
    VIDEO_DEVICE *Device;
    BITMAP_FONT_INFO *Font;
} TEXT_RENDERER;

HO_STATUS HO_KERNEL_API TrInit(TEXT_RENDERER *renderer, VIDEO_DEVICE *device, BITMAP_FONT_INFO *font);

HO_STATUS HO_KERNEL_API TrRenderChar(TEXT_RENDERER *renderer, TR_RENDER_CHAR_PARAMS *params);
