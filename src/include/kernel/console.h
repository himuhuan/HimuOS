/**
 * HimuOperatingSystem
 *
 * File: console.h
 * Description:
 * General console interface.
 *
 * NOTE: Only x86-64 architecture is supported.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <drivers/basic_color.h>

#define MAX_CONSOLE_SINKS     8
#define MAX_CONSOLE_SINK_NAME 32

#define TTY0_NAME             "tty0"
#define TTY0_SINK             ((TEXT_RENDER_SINK *)ConGetSinkByName(TTY0_NAME))

struct _CONSOLE_SINK;

/*  Console sink putc function type. returns EOF on error.  */
typedef int (*CONSOLE_SINK_PUTC)(struct _CONSOLE_SINK *self, int c);

// Console sink clear screen function type
typedef void (*CONSOLE_SINK_CLS)(struct _CONSOLE_SINK *self, COLOR32 color);

typedef struct _CONSOLE_SINK
{
    uint32_t HorizontalResolution; // If 0, the sink is treated as a stream (e.g., serial port)
    char Name[MAX_CONSOLE_SINK_NAME];
    CONSOLE_SINK_PUTC PutChar;
    CONSOLE_SINK_CLS Cls; // Opt.
} CONSOLE_SINK;

typedef struct
{
    uint8_t SinkCnt;
    CONSOLE_SINK *Sinks[MAX_CONSOLE_SINKS];
} CONSOLE;

HO_KERNEL_API void ConInit(void);

/**
 * ConAddSink - Add a console sink to the system console.
 * @param sink Pointer to the CONSOLE_SINK structure to add.
 * If the maximum number of sinks is reached, the function does nothing.
 * Each sink should have a unique name for identification.
 * @remarks CONSOLE itself does not manage the memory of the sink.
 */
HO_KERNEL_API void ConAddSink(CONSOLE_SINK *sink);

HO_KERNEL_API MAYBE_UNUSED void ConRemoveSink(const char *name);

/**
 * ConPutChar - Put a character to all console sinks.
 * @return The character written as an unsigned char cast to an int or EOF on error.
 */
HO_KERNEL_API int ConPutChar(int c);

/**
 * @brief Outputs a null-terminated string to the console.
 *
 * This function writes the provided string to the system console.
 *
 * @param s Pointer to a null-terminated string to be displayed.
 * @return The number of characters written as a 64-bit unsigned integer.
 */
HO_KERNEL_API uint64_t ConPutStr(const char *s);

/**
 * @brief Writes up to 'nc' characters from the string 's' to the console.
 *
 * This function outputs at most 'nc' characters from the null-terminated string 's'
 * to the console. If the length of 's' is less than 'nc', only the available characters
 * are written.
 *
 * @param s Pointer to the null-terminated string to be written.
 * @param nc Maximum number of characters to write from the string.
 * @return The number of characters actually written to the console.
 */
HO_KERNEL_API uint64_t ConPutStrN(const char *s, uint64_t nc);

HO_KERNEL_API uint64_t ConFormatPrint(const char *fmt, ...);

HO_KERNEL_API HO_NODISCARD CONSOLE_SINK *ConGetSinkByName(const char *name);

