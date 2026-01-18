/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/gfx_console_sink.c
 * Description:
 * Ke Layer - Graphics console sink implementation
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "gfx_console_sink.h"

static HO_STATUS
GfxConSinkGetInfo(void *self, CONSOLE_SINK_INFO *info)
{
    GFX_CONSOLE_SINK *sink = (GFX_CONSOLE_SINK *)self;
    if (!sink || !sink->Driver || !sink->Font || !info)
        return EC_ILLEGAL_ARGUMENT;

    info->CharWidth = sink->Font->Width;
    info->CharHeight = sink->Font->Height;
    info->Scale = sink->Scale;
    info->CharPerTab = 4;

    info->GridWidth = sink->Driver->HorizontalResolution / (sink->Font->Width * sink->Scale);
    info->GridHeight = sink->Driver->VerticalResolution / (sink->Font->Height * sink->Scale);

    return EC_SUCCESS;
}

static HO_STATUS
GfxConSinkPutChar(void *self, uint16_t gridX, uint16_t gridY, char c, COLOR32 fg, COLOR32 bg)
{
    GFX_CONSOLE_SINK *sink = (GFX_CONSOLE_SINK *)self;
    KE_VIDEO_DRIVER *device = sink->Driver;
    uint16_t scale = sink->Scale;
    uint32_t x = gridX * sink->Font->Width * scale;
    uint32_t y = gridY * sink->Font->Height * scale;
    VD_RENDER_RECT_PARAMS rect_params = {0};
    if (c == ' ')
    {
        rect_params.X = x;
        rect_params.Y = y;
        rect_params.Width = 8 * scale;
        rect_params.Height = sink->Font->Height * scale;
        rect_params.Color = bg;
        rect_params.Filled = 1;
        VdRenderRect(device, &rect_params);
        return EC_SUCCESS;
    }

    const uint8_t *glyph = GetGlyph(sink->Font, c);
    uint32_t xi, yi;
    for (yi = 0; yi < sink->Font->Height; yi++)
    {
        for (xi = 0; xi < 8; xi++)
        {
            if (glyph[yi] & (1 << (7 - xi)))
            {
                if (HO_LIKELY(scale == 1))
                {
                    VdRenderPixel(device, x + xi, yi + y, fg);
                }
                else
                {
                    rect_params.X = x + (xi * scale);
                    rect_params.Y = y + (yi * scale);
                    rect_params.Width = scale;
                    rect_params.Height = scale;
                    rect_params.Color = fg;
                    rect_params.Filled = 1;
                    VdRenderRect(device, &rect_params);
                }
            }
            else
            {
                if (HO_LIKELY(scale == 1))
                {
                    VdRenderPixel(device, xi + x, yi + y, bg);
                }
                else
                {
                    rect_params.X = x + (xi * scale);
                    rect_params.Y = y + (yi * scale);
                    rect_params.Width = scale;
                    rect_params.Height = scale;
                    rect_params.Color = bg;
                    rect_params.Filled = 1;
                    VdRenderRect(device, &rect_params);
                }
            }
        }
    }

    return EC_SUCCESS;
}

static HO_STATUS
GfxConSinkScroll(void *self, uint16_t count, COLOR32 fill)
{
    GFX_CONSOLE_SINK *sink = (GFX_CONSOLE_SINK *)self;
    const uint8_t kScale = 1;
    const size_t kLineSize = sink->Driver->HorizontalResolution * sink->Font->Height * kScale * 4;
    const size_t kScrollSize = kLineSize * count;
    uint8_t *dest = (uint8_t *)sink->Driver->FrameBuffer;
    uint8_t *src = dest + kScrollSize;
    memmove(dest, src, sink->Driver->FrameBufferSize - kScrollSize);
    if (!fill)
        memset(dest + (sink->Driver->FrameBufferSize - kScrollSize), 0, kScrollSize);
    else
    {
        VD_RENDER_RECT_PARAMS rect_params = {0};
        rect_params.X = 0;
        rect_params.Y = sink->Driver->VerticalResolution - (sink->Font->Height * count);
        rect_params.Width = sink->Driver->HorizontalResolution;
        rect_params.Height = sink->Font->Height * count;
        rect_params.Color = fill;
        rect_params.Filled = 1;
        VdRenderRect(sink->Driver, &rect_params);
    }
    return EC_SUCCESS;
}

static HO_STATUS
GfxConSinkClear(void *self, COLOR32 fillColor)
{
    GFX_CONSOLE_SINK *sink = (GFX_CONSOLE_SINK *)self;
    VD_RENDER_RECT_PARAMS rect_params = {0};
    rect_params.X = 0;
    rect_params.Y = 0;
    rect_params.Width = sink->Driver->HorizontalResolution;
    rect_params.Height = sink->Driver->VerticalResolution;
    rect_params.Color = fillColor;
    rect_params.Filled = 1;
    VdRenderRect(sink->Driver, &rect_params);
    return EC_SUCCESS;
}

void KeGfxConSinkInit(GFX_CONSOLE_SINK *sink, KE_VIDEO_DRIVER *driver, BITMAP_FONT_INFO *font, uint8_t scale)
{
    sink->Base.GetInfo = GfxConSinkGetInfo;
    sink->Base.PutChar = GfxConSinkPutChar;
    sink->Base.Scroll = GfxConSinkScroll;
    sink->Base.Clear = GfxConSinkClear;
    sink->Driver = driver;
    sink->Font = font;
    sink->Scale = scale;
}