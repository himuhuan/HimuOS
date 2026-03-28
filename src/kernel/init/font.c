/**
 * HimuOperatingSystem
 *
 * File: init/font.c
 * Description: Bitmap font setup for early console initialization.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "init_internal.h"

void
InitBitmapFont(void)
{
    gSystemFont.Width = FONT_WIDTH;
    gSystemFont.Height = FONT_HEIGHT;
    gSystemFont.GlyphCount = FONT_GLYPH_COUNT;
    gSystemFont.RawGlyphs = (const uint8_t *)gFont8x16Data;
    gSystemFont.RowStride = 1u;
    gSystemFont.GlyphStride = 16u;
}
