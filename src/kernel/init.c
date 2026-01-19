#include <kernel/init.h>
#include <drivers/serial.h>
#include <kernel/hodbg.h>
#include <arch/amd64/idt.h> // TODO: remove dependency on x86 arch
#include <arch/amd64/acpi.h>
#include "assets/fonts/font8x16.h"

//
// Global kernel variables that need to be initialized at startup
//

KE_VIDEO_DRIVER gVideoDriver;
ARCH_BASIC_CPU_INFO gBasicCpuInfo;
BITMAP_FONT_INFO gSystemFont;

static void InitBitmapFont(void);
static void InitCpuState(STAGING_BLOCK *block);
static void AssertRsdp(HO_VIRTUAL_ADDRESS rsdpVirt);

void
InitKernel(MAYBE_UNUSED STAGING_BLOCK *block)
{
    InitCpuState(block);
    InitBitmapFont();
    VdInit(&gVideoDriver, block);
    VdClearScreen(&gVideoDriver, COLOR_BLACK);
    ConsoleInit(&gVideoDriver, &gSystemFont);

    HO_STATUS initStatus;
    initStatus = IdtInit();
    if (initStatus != EC_SUCCESS)
    {
        kprintf("FATAL: HimuOS initialization failed!\n");
        while (1)
            ;
    }
    AssertRsdp(HHDM_PHYS2VIRT(block->AcpiRsdpPhys));
    GetBasicCpuInfo(&gBasicCpuInfo);
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
    CPU_CORE_LOCAL_DATA *data = &block->CpuInfo;
    data->Tss.RSP0 = HHDM_PHYS2VIRT(block->KrnlStackPhys) + block->Layout.KrnlStackSize;
    data->Tss.IST1 = HHDM_PHYS2VIRT(block->KrnlStackPhys) + block->Layout.IST1StackSize;
    data->Tss.IOMapBase = sizeof(TSS64); // No IO permission bitmap
}

static void
AssertRsdp(HO_VIRTUAL_ADDRESS rsdpVirt)
{
    ACPI_RSDP *rsdp = (void *)rsdpVirt;
    if (rsdp->Signature[0] != 'R' || rsdp->Signature[1] != 'S' || rsdp->Signature[2] != 'D' ||
        rsdp->Signature[3] != ' ' || rsdp->Signature[4] != 'P' || rsdp->Signature[5] != 'T' ||
        rsdp->Signature[6] != 'R')
    {
        HO_KPANIC(EC_NOT_SUPPORTED, "ACPI RSDP signature invalid");
    }

    if (rsdp->Revision < 2)
    {
        HO_KPANIC(EC_NOT_SUPPORTED, "ACPI Revision not supported (only v2.0+ supported)");
    }
}