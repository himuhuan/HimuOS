#pragma once

/**
 * HimuOperatingSystem
 *
 * File: text_sink.h
 * Description: Text sink interface for TUI.
 * Copyright(c) 2024-2025 HimuOperatingSystem, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <lib/tui/text_render.h>
#include <kernel/console.h>

typedef struct
{
    CONSOLE_SINK Base;
    TEXT_RENDERER *Renderer;
    uint32_t CursorX, CursorY;
    COLOR32 ForegroundColor;
    COLOR32 BackgroundColor;
} TEXT_RENDER_SINK;

typedef enum
{
    TR_PUTS_ALIGN_LEFT,
    TR_PUTS_ALIGN_CENTER,
    TR_PUTS_ALIGN_RIGHT
} TR_PUTS_ALIGNMENT;

HO_KERNEL_API void TrSinkInit(const char *name, TEXT_RENDER_SINK *sink, TEXT_RENDERER *renderer);
HO_KERNEL_API int TrSinkPutChar(CONSOLE_SINK *sink, int c);
HO_KERNEL_API void TrSinkClearScreen(CONSOLE_SINK *sink, COLOR32 color);

HO_KERNEL_API void TrSinkSetAlign(TEXT_RENDER_SINK *sink, uint32_t len, TR_PUTS_ALIGNMENT align);
HO_KERNEL_API void TrSinkGetColor(TEXT_RENDER_SINK *sink, COLOR32 *fg, COLOR32 *bg);
HO_KERNEL_API void TrSinkSetColor(TEXT_RENDER_SINK *sink, COLOR32 fg, COLOR32 bg);