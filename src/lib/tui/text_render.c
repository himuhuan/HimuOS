#include "lib/tui/text_render.h"

HO_STATUS
TrInit(TEXT_RENDERER *renderer, VIDEO_DEVICE *device, BITMAP_FONT_INFO *font)
{
    if (!renderer || !device || !font)
        return EC_ILLEGAL_ARGUMENT;

    renderer->Device = device;
    renderer->Font = font;

    return EC_SUCCESS;
}

HO_STATUS HO_KERNEL_API
TrRenderChar(TEXT_RENDERER *renderer, TR_RENDER_CHAR_PARAMS *params)
{
    if (!renderer || !params || !renderer->Device || !renderer->Font)
        return EC_ILLEGAL_ARGUMENT;
    if (params->Char >= renderer->Font->GlyphCount)
        return EC_ILLEGAL_ARGUMENT; // Invalid character code

    VIDEO_DEVICE *device = renderer->Device;
    uint16_t scale = (params->Scale == 0) ? 1 : params->Scale;
    VD_RENDER_RECT_PARAMS rect_params = {0};
    if (params->Char == ' ')
    {
        rect_params.X = params->X;
        rect_params.Y = params->Y;
        rect_params.Width = 8 * scale;
        rect_params.Height = renderer->Font->Height * scale;
        rect_params.Color = params->BackgroundColor;
        rect_params.Filled = 1;
        VdRenderRect(device, &rect_params);
        return EC_SUCCESS;
    }

    const uint8_t *glyph = GetGlyph(renderer->Font, params->Char);
    uint32_t x, y;
    for (y = 0; y < renderer->Font->Height; y++)
    {
        // TODO: Support fonts wider than 8 pixels
        for (x = 0; x < 8; x++)
        {
            if (glyph[y] & (1 << (7 - x)))
            {
                if (HO_LIKELY(scale == 1))
                {
                    VdRenderPixel(device, params->X + x, params->Y + y, params->TextColor);
                }
                else
                {
                    // Scale rendering
                    rect_params.X = params->X + (x * scale);
                    rect_params.Y = params->Y + (y * scale);
                    rect_params.Width = scale;
                    rect_params.Height = scale;
                    rect_params.Color = params->TextColor;
                    rect_params.Filled = 1;
                    VdRenderRect(device, &rect_params);
                }
            }
            else
            {
                if (HO_LIKELY(scale == 1))
                {
                    VdRenderPixel(device, params->X + x, params->Y + y, params->BackgroundColor);
                }
                else
                {
                    // Scale rendering
                    rect_params.X = params->X + (x * scale);
                    rect_params.Y = params->Y + (y * scale);
                    rect_params.Width = scale;
                    rect_params.Height = scale;
                    rect_params.Color = params->BackgroundColor;
                    rect_params.Filled = 1;
                    VdRenderRect(device, &rect_params);
                }
            }
        }
    }

    return EC_SUCCESS;
}
