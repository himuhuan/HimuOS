/**
 * HimuOperatingSystem PUBLIC HEADER
 * 
 * File: console.h
 * Description: Console APIs
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include <drivers/video_driver.h>
#include <lib/tui/bitmap_font.h>

struct CONSOLE_DEVICE; // Opaque
typedef struct CONSOLE_DEVICE CONSOLE_DEVICE;

HO_PUBLIC_API HO_STATUS ConsoleInit(VIDEO_DRIVER *driver, BITMAP_FONT_INFO *font);

HO_PUBLIC_API CONSOLE_DEVICE* ConsoleGetGlobalDevice(void);

HO_PUBLIC_API int ConsoleWriteChar(char c);

HO_PUBLIC_API uint64_t ConsoleWrite(const char *str);

HO_PUBLIC_API uint64_t ConsoleWriteFmt(const char *fmt, ...);


