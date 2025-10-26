#include "video_efi.h"

static uint32_t HO_NODISCARD
EfiToPhysColor(VIDEO_DRIVER *self, COLOR32 color)
{
    uint8_t r = GET_RED_PART(color);
    uint8_t g = GET_GREEN_PART(color);
    uint8_t b = GET_BLUE_PART(color);

    if (self->Format == PIXEL_FORMAT_BGR)
    {
        return (r << 16) | (g << 8) | b;
    }
    else if (self->Format == PIXEL_FORMAT_RGB)
    {
        return (b << 16) | (g << 8) | r;
    }

    // Unknown format
    return 0;
}

static HO_STATUS HO_NODISCARD
EfiClearScreen(VIDEO_DRIVER *self, COLOR32 color)
{
    if (self == NULL || self->FrameBuffer == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    uint32_t physColor = EfiToPhysColor(self, color);
    uint64_t *frameBuffer = (uint64_t *)self->FrameBuffer;
    uint64_t pixelCount = self->FrameBufferSize / sizeof(COLOR32);

    // TODO: SIMD optimization
    uint64_t color64 = ((uint64_t)physColor << 32) | physColor;
    uint64_t cnt = pixelCount / 2;
    for (uint64_t i = 0; i < cnt; ++i)
        frameBuffer[i] = color64;

    return EC_SUCCESS;
}

static HO_STATUS HO_NODISCARD
EfiRenderPixel(VIDEO_DRIVER *self, uint32_t x, uint32_t y, COLOR32 color)
{
    uint32_t hr = self->HorizontalResolution;
    if (x >= hr || y >= self->VerticalResolution)
    {
        return EC_ILLEGAL_ARGUMENT; // Out of bounds
    }

    uint32_t physColor = EfiToPhysColor(self, color);
    uint32_t *frameBuffer = (uint32_t *)self->FrameBuffer;
    frameBuffer[y * hr + x] = physColor;

    return EC_SUCCESS;
}

static HO_STATUS HO_NODISCARD
EfiRenderRect(VIDEO_DRIVER *self, VD_RENDER_RECT_PARAMS *params)
{
    uint32_t x, y;
    HO_STATUS status;

    if (params->Filled)
    {
        for (y = 0; y < params->Height; ++y)
        {
            for (x = 0; x < params->Width; ++x)
            {
                status = EfiRenderPixel(self, params->X + x, params->Y + y, params->Color);
                if (status != EC_SUCCESS)
                {
                    // If a pixel is out of bounds, we can stop rendering.
                    // The caller can decide if this is a critical error.
                    return status;
                }
            }
        }
    }
    else
    {
        // If not filled, render only the border of the rectangle
        for (x = 0; x < params->Width; x++)
        {
            status = EfiRenderPixel(self, params->X + x, params->Y, params->Color); // Top border
            if (status != EC_SUCCESS)
                return status;
            status =
                EfiRenderPixel(self, params->X + x, params->Y + params->Height - 1, params->Color); // Bottom border
            if (status != EC_SUCCESS)
                return status;
        }
        for (y = 0; y < params->Height; y++)
        {
            status = EfiRenderPixel(self, params->X, params->Y + y, params->Color); // Left border
            if (status != EC_SUCCESS)
                return status;

            status = EfiRenderPixel(self, params->X + params->Width - 1, params->Y + y, params->Color); // Right border
            if (status != EC_SUCCESS)
                return status;
        }
    }

    return EC_SUCCESS;
}

static const VD_VTABLE kEfiVTable = {
    .ToPhysColor = EfiToPhysColor,
    .ClearScreen = EfiClearScreen,
    .RenderPixel = EfiRenderPixel,
    .RenderRect = EfiRenderRect,
};

const VD_VTABLE *
VdEfiGetVTable(void)
{
    return &kEfiVTable;
}