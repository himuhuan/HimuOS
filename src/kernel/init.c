#include <kernel/init.h>
#include <kernel/console.h>
#include <drivers/serial.h>
#include <kernel/hodbg.h>
#include <arch/amd64/idt.h>
#include "assets/fonts/font8x16.h"

//
// Global kernel variables that need to be initialized at startup
//

VIDEO_DRIVER gVideoDevice;
BITMAP_FONT_INFO gSystemFont;
static void InitBitmapFont(void);

#define DO_INIT_PROCESS(status, routine, ...)                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        kprintf("[INIT] %s... ", #routine);                                                                            \
        status = routine(##__VA_ARGS__);                                                                               \
        if (!status)                                                                                                   \
            kprintf(ANSI_FG_GREEN "OK\n" ANSI_RESET);                                                                  \
        else                                                                                                           \
            kprintf(ANSI_FG_RED "FAILED: %ks\n" ANSI_RESET, status);                                                   \
    } while (FALSE)

void
InitKernel(MAYBE_UNUSED STAGING_BLOCK *block)
{
    InitBitmapFont();
    VdInit(&gVideoDevice, block);
    VdClearScreen(&gVideoDevice, COLOR_BLACK);
    ConsoleInit(&gVideoDevice, &gSystemFont);

    HO_STATUS initStatus;
    DO_INIT_PROCESS(initStatus, IdtInit);

    if (initStatus != EC_SUCCESS)
    {
        kprintf("FATAL: HimuOS initialzation failed!\n");
        while (1)
            ;
    }

    kprintf("Himu Operating System VERSION %s\n", KRNL_VERSTR);
    kprintf("Copyright(c) 2024-2025 Himu, ONLY FOR EDUCATIONAL PURPOSES.\n\n");
    kprintf("Staging Block: \t\t\t%p\n", block->BaseVirt);
    kprintf("Total Usable Memory: \t\t%i bytes\n", block->TotalReclaimableMem);

    HO_KASSERT(1 == 2, EC_UNREACHABLE);
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