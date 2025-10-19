#include "drivers/video_device.h"
#include "libc/string.h"
#include <kernel/hodbg.h>
#include "efi/video_efi.h"

void HO_KERNEL_API
VdInit(VIDEO_DEVICE *pd, BOOT_INFO_HEADER *info)
{
    if (pd == NULL || info == NULL)
    {
        return;
    }

    memset(pd, 0, sizeof(VIDEO_DEVICE));
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
    pd->FrameBuffer = (void *)info->FramebufferPhys;
    pd->FrameBufferSize = info->FramebufferSize;
    pd->_Details = VdEfiGetVTable();
}

HO_STATUS HO_KERNEL_API
VdRenderPixel(VIDEO_DEVICE *device, uint32_t x, uint32_t y, COLOR32 color)
{
    if (device == NULL)
        return EC_ILLEGAL_ARGUMENT;
    if (device->_Details->RenderPixel == NULL)
        return EC_NOT_SUPPORTED;

    return device->_Details->RenderPixel(device, x, y, color);
}

HO_STATUS HO_KERNEL_API
VdRenderRect(VIDEO_DEVICE *device, VD_RENDER_RECT_PARAMS *params)
{
    if (device == NULL || params == NULL)
        return EC_ILLEGAL_ARGUMENT;
    if (device->_Details->RenderRect == NULL)
        return EC_NOT_SUPPORTED;

    return device->_Details->RenderRect(device, params);
}

HO_STATUS HO_KERNEL_API
VdClearScreen(VIDEO_DEVICE *device, uint32_t color)
{
    if (device == NULL)
        return EC_ILLEGAL_ARGUMENT;
    if (device->_Details->ClearScreen == NULL)
        return EC_NOT_SUPPORTED;

    return device->_Details->ClearScreen(device, color);
}