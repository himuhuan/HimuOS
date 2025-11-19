#include <kernel/console.h>
#include <string.h>
#include <stdarg.h>
#include <kernel/hodbg.h>
#include <hostdlib.h>
#include "console_device.h"
#include "sinks/gfx_console_sink.h"
#include "sinks/mux_console_sink.h"
#include "sinks/serial_console_sink.h"

#define MAX_FORMAT_BUFFER 64

//
// Global Variables
//

static CONSOLE_DEVICE gConsoleDevice;
static GFX_CONSOLE_SINK gGfxConsoleSink;
#if __HO_DEBUG_BUILD__
static SERIAL_CONSOLE_SINK gSerialConsoleSink;
static MUX_CONSOLE_SINK gMuxConsoleSink;
#endif
static BOOL gConsoleInitialized = FALSE;

HO_PUBLIC_API HO_STATUS
ConsoleInit(VIDEO_DRIVER *driver, BITMAP_FONT_INFO *font)
{
    if (gConsoleInitialized)
        return EC_SUCCESS;
#ifndef __HO_DEBUG_BUILD__
    // In release builds, we only use GfxConSink
    GfxConSinkInit(&gGfxConsoleSink, driver, font, 1);
    ConDevInit(&gConsoleDevice, (CONSOLE_SINK *)&gGfxConsoleSink);
#else
    MuxConSinkInit(&gMuxConsoleSink);
    GfxConSinkInit(&gGfxConsoleSink, driver, font, 1);
    MuxConSinkAddSink(&gMuxConsoleSink, (CONSOLE_SINK *)&gGfxConsoleSink); // As primary sink
    SerialConSinkInit(&gSerialConsoleSink, COM1_PORT);
    MuxConSinkAddSink(&gMuxConsoleSink, (CONSOLE_SINK *)&gSerialConsoleSink);
    ConDevInit(&gConsoleDevice, (CONSOLE_SINK *)&gMuxConsoleSink);
#endif
    gConsoleInitialized = TRUE;
    return EC_SUCCESS;
}

HO_PUBLIC_API CONSOLE_DEVICE *
ConsoleGetGlobalDevice(void)
{
    return &gConsoleDevice;
}

HO_PUBLIC_API int
ConsoleWriteChar(char c)
{
    if (!gConsoleInitialized)
        return -1;

    return ConDevPutChar(&gConsoleDevice, c);
}

HO_PUBLIC_API uint64_t
ConsoleWrite(const char *str)
{
    if (!gConsoleInitialized)
        return 0;

    return ConDevPutStr(&gConsoleDevice, str);
}

HO_PUBLIC_API uint64_t
ConsoleWriteFmt(const char *fmt, ...)
{
    VA_LIST args;
    VA_START(args, fmt);
    char buf[MAX_FORMAT_BUFFER];
    uint64_t written = 0;

    const char *p;
    for (p = fmt; *p; ++p)
    {
        if (*p != '%')
        {
            (void)ConsoleWriteChar(*p);
            written++;
            continue;
        }

        ++p; // Skip '%'

        if (*p == '%')
        {
            (void)ConsoleWriteChar(*p);
            written++;
            ++p;
            continue;
        }

        // Parse padding
        char pc = 0;

        if (*p == '0')
        {
            pc = '0';
            ++p;
        }

        uint32_t width = 0;
        while (*p >= '0' && *p <= '9')
        {
            width = width * 10 + (*p - '0');
            p++;
        }
        if (width > 0 && !pc)
            pc = ' ';

        switch (*p)
        {
        case 'c': {
            char c = (char)VA_ARG(args, int);
            (void)ConsoleWriteChar(c);
            written++;
            break;
        }
        case 's': {
            const char *s = VA_ARG(args, const char *);
            if (s == NULL)
                s = "(null)";
            if (width)
            {
                size_t len = strlen(s);
                if (len < width)
                {
                    for (uint32_t i = 0; i < width - len; ++i)
                    {
                        (void)ConsoleWriteChar(pc);
                        written++;
                    }
                }
            }
            written += ConsoleWrite(s);
            break;
        }
        case 'd':
        case 'i': {
            int64_t val = VA_ARG(args, int);
            (void) Int64ToStringEx(val, buf, width, pc);
            written += ConsoleWrite(buf);
            break;
        }
        case 'l': {
            if (*(p + 1) == 'd' || *(p + 1) == 'i') // long decimal
            {
                ++p;
                int64_t val = VA_ARG(args, long);
                (void)Int64ToStringEx(val, buf, width, pc);
                written += ConsoleWrite(buf);
            }
            else if (*(p + 1) == 'u') // long unsigned
            {
                ++p;
                uint64_t val = VA_ARG(args, unsigned long);
                (void)UInt64ToStringEx(val, buf, 10, width, pc);
                written += ConsoleWrite(buf);
            }
            else if (*(p + 1) == 'x' || *(p + 1) == 'X') // long hex
            {
                ++p;
                uint64_t val = VA_ARG(args, unsigned long);
                (void)UInt64ToStringEx(val, buf, 16, width, pc);
                written += ConsoleWrite(buf);
            }
            else
            {
                HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Unsupported format in kernel printf");
            }
            break;
        }
        case 'u': {
            uint64_t val = VA_ARG(args, unsigned int);
            (void)UInt64ToString(val, buf, 10, FALSE);
            written += ConsoleWrite(buf);
            break;
        }
        case 'x':
        case 'X': {
            uint64_t val = VA_ARG(args, uint64_t);
            (void)UInt64ToString(val, buf, 16, FALSE);
            written += ConsoleWrite(buf);
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)VA_ARG(args, void *);
            written += ConsoleWrite("0X");
            (void)UInt64ToString(val, buf, 16, TRUE);
            written += ConsoleWrite(buf);
            break;
        }
        // HimuOS kernel specific placeholders
        case 'k': {
            if (*(p + 1) == 'e') // error codes
            {
                ++p;
                HO_STATUS val = VA_ARG(args, HO_STATUS);
                const char *msg = KrGetStatusMessage(val);
                written += ConsoleWrite(msg);
            }
            else if (*(p + 1) == 's') // error codes status (FAIL or SUCCESS)
            {
                ++p;
                HO_STATUS val = VA_ARG(args, HO_STATUS);
                const char *msg = HO_LIKELY(!val) ? "OK" : "FAILED";
                written += ConsoleWrite(msg);
            }
            else
            {
                HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Unsupported format in kernel printf");
            }
            break;
        }
        default:
            // Break change: invalid format specifier will trigger KERNEL PANIC!
            HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Unsupported format in kernel printf");
            break;
        }
    }
    VA_END(args);
    return written;
}

HO_PUBLIC_API void ConsoleClearScreen(COLOR32 color)
{
    if (!gConsoleInitialized)
        return;

    ConDevClearScreen(&gConsoleDevice, color);
}