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
#include <stdarg.h>

// ANSI Colors

#if defined(HO_DISABLE_COLOR_TUI)

#define ANSI_RESET          ""

#define ANSI_FG_BLACK       ""
#define ANSI_FG_RED         ""
#define ANSI_FG_GREEN       ""
#define ANSI_FG_YELLOW      ""
#define ANSI_FG_BLUE        ""
#define ANSI_FG_MAGENTA     ""
#define ANSI_FG_CYAN        ""
#define ANSI_FG_WHITE       ""

#define ANSI_BG_BLACK       ""
#define ANSI_BG_RED         ""
#define ANSI_BG_GREEN       ""
#define ANSI_BG_YELLOW      ""
#define ANSI_BG_BLUE        ""
#define ANSI_BG_MAGENTA     ""
#define ANSI_BG_CYAN        ""
#define ANSI_BG_WHITE       ""

#else
#define ANSI_RESET          "\x1B[0m"

#define ANSI_FG_BLACK       "\x1B[30m"
#define ANSI_FG_RED         "\x1B[31m"
#define ANSI_FG_GREEN       "\x1B[32m"
#define ANSI_FG_YELLOW      "\x1B[33m"
#define ANSI_FG_BLUE        "\x1B[34m"
#define ANSI_FG_MAGENTA     "\x1B[35m"
#define ANSI_FG_CYAN        "\x1B[36m"
#define ANSI_FG_WHITE       "\x1B[37m"

#define ANSI_BG_BLACK       "\x1B[40m"
#define ANSI_BG_RED         "\x1B[41m"
#define ANSI_BG_GREEN       "\x1B[42m"
#define ANSI_BG_YELLOW      "\x1B[43m"
#define ANSI_BG_BLUE        "\x1B[44m"
#define ANSI_BG_MAGENTA     "\x1B[45m"
#define ANSI_BG_CYAN        "\x1B[46m"
#define ANSI_BG_WHITE       "\x1B[47m"
#endif

struct KE_CONSOLE_DEVICE; // Opaque
typedef struct KE_CONSOLE_DEVICE KE_CONSOLE_DEVICE;

HO_PUBLIC_API HO_STATUS ConsoleInit(KE_VIDEO_DRIVER *driver, BITMAP_FONT_INFO *font);

HO_PUBLIC_API KE_CONSOLE_DEVICE* ConsoleGetGlobalDevice(void);

HO_PUBLIC_API int ConsoleWriteChar(char c);

HO_PUBLIC_API uint64_t ConsoleWrite(const char *str);

HO_PUBLIC_API uint64_t ConsoleWriteFmt(const char *fmt, ...);
HO_PUBLIC_API uint64_t ConsoleWriteVFmt(const char *fmt, VA_LIST args);

HO_PUBLIC_API void ConsoleClearScreen(COLOR32 color);
