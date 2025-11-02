/**
 * HimuOperatingSystem
 *
 * File: basic_color.h
 * Description:
 * This header defines basic color structures and constants for the device video subsystem.
 *
 * NOTE: These defines ONLY WORKS with Little ENDIAN systems.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

/* In HimuOS, we use ARGB color syntax: 0xAARRGGBB */
typedef uint32_t COLOR32;

#define MAKE_COLOR32(r, g, b, a) \
    ((COLOR32)((((a)&0xFF) << 24) | (((r)&0xFF) << 16) | (((g)&0xFF) << 8) | ((b)&0xFF)))

// common colors

#define COLOR_BLACK MAKE_COLOR32(0x00, 0x00, 0x00, 0x00)
#define COLOR_WHITE MAKE_COLOR32(0xFF, 0xFF, 0xFF, 0x00)
#define COLOR_RED MAKE_COLOR32(0xFF, 0x00, 0x00, 0x00)
#define COLOR_GREEN MAKE_COLOR32(0x00, 0xFF, 0x00, 0x00)
#define COLOR_BLUE MAKE_COLOR32(0x00, 0x00, 0xFF, 0x00)
#define COLOR_YELLOW MAKE_COLOR32(0xFF, 0xFF, 0x00, 0x00)
#define COLOR_MAGENTA MAKE_COLOR32(0xFF, 0x00, 0xFF, 0x00)
#define COLOR_CYAN    MAKE_COLOR32(0x00, 0xFF, 0xFF, 0x00)

#define COLOR_TEXT MAKE_COLOR32(152, 152, 152, 0x00)

#define GET_RED_PART(color) (((color) >> 16) & 0xFF)
#define GET_GREEN_PART(color) (((color) >> 8) & 0xFF)
#define GET_BLUE_PART(color) ((color)&0xFF)
#define GET_ALPHA_PART(color) (((color) >> 24) & 0xFF)
