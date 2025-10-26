#include <kernel/init.h>
#include <kernel/console.h>
#include <drivers/serial.h>
#include <kernel/hodbg.h>
#include "assets/fonts/font8x16.h"

//
// Global kernel variables that need to be initialized at startup
//

VIDEO_DRIVER gVideoDevice;
BITMAP_FONT_INFO gSystemFont;
static void InitBitmapFont(void);

void
InitKernel(MAYBE_UNUSED STAGING_BLOCK *block)
{
    InitBitmapFont();
    VdInit(&gVideoDevice, block);
    VdClearScreen(&gVideoDevice, COLOR_BLACK);
    ConsoleInit(&gVideoDevice, &gSystemFont);

    kprintf("Himu Operating System VERSION %s\n", KRNL_VERSTR);
    kprintf("Copyright(c) 2024-2025 Himu, ONLY FOR EDUCATIONAL PURPOSES.\n\n");
    kprintf("Staging Block: \t\t\t%p\n", block->BaseVirt);
    kprintf("Total Usable Memory: \t\t%i bytes\n", block->TotalReclaimableMem);
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