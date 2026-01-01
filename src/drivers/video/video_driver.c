#include "drivers/video_driver.h"
#include "libc/string.h"
#include <kernel/hodbg.h>
#include "efi/video_efi.h"
#include "kernel/hodefs.h"

void HO_KERNEL_API
VdInit(VIDEO_DRIVER *pd, STAGING_BLOCK *info)
{
    if (pd == NULL || info == NULL)
    {
        return;
    }

    memset(pd, 0, sizeof(VIDEO_DRIVER));
    pd->Type = info->VideoModeType;
    kprintf("Video mode type: %d\n", pd->Type);
    if (pd->Type != VIDEO_MODE_TYPE_UEFI)
    {
        // Unsupported video mode type
        kprintf("Unsupported video mode type: %d\n", pd->Type);
        return;
    }

    pd->Format = info->PixelFormat;
    pd->HorizontalResolution = info->HorizontalResolution;
    pd->VerticalResolution = info->VerticalResolution;
    pd->PixelsPerScanLine = info->PixelsPerScanLine;
    pd->FrameBuffer = (void *) MMIO_BASE_VA;
    pd->FrameBufferSize = info->FramebufferSize;
    pd->Methods = VdEfiGetVTable();
}

HO_STATUS HO_KERNEL_API
VdRenderPixel(VIDEO_DRIVER *device, uint32_t x, uint32_t y, COLOR32 color)
{
    if (device == NULL)
        return EC_ILLEGAL_ARGUMENT;
    if (device->Methods->RenderPixel == NULL)
        return EC_NOT_SUPPORTED;

    return device->Methods->RenderPixel(device, x, y, color);
}

HO_STATUS HO_KERNEL_API
VdRenderRect(VIDEO_DRIVER *device, VD_RENDER_RECT_PARAMS *params)
{
    if (device == NULL || params == NULL)
        return EC_ILLEGAL_ARGUMENT;
    if (device->Methods->RenderRect == NULL)
        return EC_NOT_SUPPORTED;

    return device->Methods->RenderRect(device, params);
}

HO_STATUS HO_KERNEL_API
VdClearScreen(VIDEO_DRIVER *device, uint32_t color)
{
    if (device == NULL)
        return EC_ILLEGAL_ARGUMENT;
    if (device->Methods->ClearScreen == NULL)
        return EC_NOT_SUPPORTED;

    return device->Methods->ClearScreen(device, color);
}