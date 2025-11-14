#include <kernel/init.h>
#include <kernel/console.h>
#include <drivers/serial.h>
#include <kernel/hodbg.h>
#include <arch/arch.h>
#include <arch/amd64/idt.h> // TODO: remove dependency on x86 arch
#include <arch/amd64/pm.h>
#include "assets/fonts/font8x16.h"

//
// Global kernel variables that need to be initialized at startup
//

VIDEO_DRIVER gVideoDevice;
ARCH_BASIC_CPU_INFO gBasicCpuInfo;
BITMAP_FONT_INFO gSystemFont;

static void InitBitmapFont(void);
static void InitCpuState(STAGING_BLOCK *block);

void
InitKernel(MAYBE_UNUSED STAGING_BLOCK *block)
{
    InitCpuState(block);
    InitBitmapFont();
    VdInit(&gVideoDevice, block);
    VdClearScreen(&gVideoDevice, COLOR_BLACK);
    ConsoleInit(&gVideoDevice, &gSystemFont);

    HO_STATUS initStatus;
    initStatus = IdtInit();
    if (initStatus != EC_SUCCESS)
    {
        kprintf("FATAL: HimuOS initialization failed!\n");
        while (1)
            ;
    }

    GetBasicCpuInfo(&gBasicCpuInfo);

    kprintf("Total Usable Memory:     %i bytes\n", block->TotalReclaimableMem);
    kprintf("CPU:                     %s (x86_64)\n", gBasicCpuInfo.ModelName);
    kprintf("Timer Features\n");
    kprintf(" * Counter:              %s\n", gBasicCpuInfo.TimerFeatures & ARCH_TIMER_FEAT_COUNTER ? "YES" : "NOT SUPPORTED");
    kprintf(" * Invariant counter:    %s\n",
            gBasicCpuInfo.TimerFeatures & ARCH_TIMER_FEAT_INVARIANT ? "YES" : "NOT SUPPORTED");
    kprintf(" * Deadline mode:        %s\n\n",
            gBasicCpuInfo.TimerFeatures & ARCH_TIMER_FEAT_ONE_SHOT ? "YES" : "NOT SUPPORTED");

    kprintf("Himu Operating System VERSION %s\n", KRNL_VERSTR);
    kprintf("Copyright(c) 2024-2025 Himu, ONLY FOR EDUCATIONAL PURPOSES.\n\n");
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

static void
InitCpuState(STAGING_BLOCK *block)
{
    CPU_CORE_LOCAL_DATA *data = (CPU_CORE_LOCAL_DATA *)block->CoreLocalDataVirt;
    data->Tss.RSP0 = block->KrnlStackVirt + block->KrnlStackSize;
    data->Tss.IST1 = block->KrnlIST1StackVirt + block->KrnlStackSize;
    data->Tss.IOMapBase = sizeof(TSS64); // No IO permission bitmap
}