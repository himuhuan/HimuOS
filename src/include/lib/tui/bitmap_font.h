/**
 * HimuOperatingSystem
 *
 * File: bitmap_font.h
 * Description:
 * Bitmap font definitions for TUI (Text User Interface).
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "_hobase.h"

typedef struct
{
    uint8_t Width, Height;
    uint16_t GlyphCount;      // Number of characters in the font
    uint16_t RowStride;       // Bytes between consecutive rows of a glyph
    uint32_t GlyphStride;     // Bytes between the start of glyph i and glyph i+1.
    const uint8_t *RawGlyphs; // Pointer to the "flat" and continuous glyph data
} BITMAP_FONT_INFO;

MAYBE_UNUSED static inline const uint8_t *
GetGlyph(const BITMAP_FONT_INFO *font, uint16_t glyphIndex)
{
    if (glyphIndex >= font->GlyphCount)
        return NULL; // Out of bounds
    return font->RawGlyphs + glyphIndex * font->GlyphStride;
}