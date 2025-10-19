#include <kernel/init.h>
#include <kernel/console.h>
#include <drivers/serial.h>
#include <kernel/hodbg.h>
#include <lib/tui/tui.h>
#include "assets/fonts/font8x16.h"

//
// Global kernel variables that need to be initialized at startup
//

VIDEO_DEVICE gVideoDevice;
TEXT_RENDERER gTextRenderer;
CONSOLE_SINK gSerialConsoleSink;
TEXT_RENDER_SINK gTextRenderSink;
BITMAP_FONT_INFO gSystemFont;

static void AddSerialDebugConsole(void);
static void AddTextConsole(void);
static void InitBitmapFont(void);

void
InitKernel(MAYBE_UNUSED STAGING_BLOCK *block)
{
    AddSerialDebugConsole();
    VdInit(&gVideoDevice, block);
    AddTextConsole();
    VdClearScreen(&gVideoDevice, COLOR_BLACK);
    kprintf("\nHimu Operating System VERSION %s\n", KRNL_VERSTR);
    kprintf("Copyright(c) 2024-2025 Himu, ONLY FOR EDUCATIONAL PURPOSES.\n\n");
    KrPrintHexMessage("Staging Block", block->BaseVirt);
    KrPrintHexMessage("Staging Block Size", block->Size);
    KrPrintHexMessage("Total System Memory", block->TotalReclaimableMem);
}

static int
SerialPutCharSink(CONSOLE_SINK *sink, int c)
{
    (void)sink;
    SerialWriteByte(PORT_COM1, (char)c);
    return c;
}

static void
AddSerialDebugConsole(void)
{
    SerialInit(PORT_COM1);
    gSerialConsoleSink.HorizontalResolution = 0;
    gSerialConsoleSink.PutChar = SerialPutCharSink;
    strcpy(gSerialConsoleSink.Name, "serial-debug");
    ConAddSink(&gSerialConsoleSink);
#ifndef NDEBUG
    KrPrintStautsMessage("Serial Debug Console", EC_SUCCESS);
#endif
}

MAYBE_UNUSED static void
AddTextConsole(void)
{
    HO_STATUS status;
    InitBitmapFont();
    status = TrInit(&gTextRenderer, &gVideoDevice, &gSystemFont);
    KrPrintStautsMessage("Text Renderer Initialization", status);
    TrSinkInit(TTY0_NAME, &gTextRenderSink, &gTextRenderer);
    ConAddSink((CONSOLE_SINK *)&gTextRenderSink);
#ifndef NDEBUG
    KrPrintStautsMessage("Text Console", EC_SUCCESS);
#endif
}

static void
InitBitmapFont(void)
{
    gSystemFont.Width = FONT_WIDTH;
    gSystemFont.Height = FONT_HEIGHT;
    gSystemFont.GlyphCount = FONT_GLYPH_COUNT;
    gSystemFont.RawGlyphs = (const uint8_t *)gFont8x16Data;
    gSystemFont.RowStride = 1u;
    gSystemFont.GlyphStride = 16u;
}