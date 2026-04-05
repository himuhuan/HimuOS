#include <kernel/ke/console.h>
#include <kernel/ke/critical_section.h>
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

static KE_CONSOLE_DEVICE gConsoleDevice;
static GFX_CONSOLE_SINK gGfxConsoleSink;
#if __HO_DEBUG_BUILD__
static SERIAL_CONSOLE_SINK gSerialConsoleSink;
static MUX_CONSOLE_SINK gMuxConsoleSink;
#endif
static BOOL gConsoleInitialized = FALSE;

static inline int
ConsoleWriteCharUnlocked(char c)
{
    return KeConDevPutChar(&gConsoleDevice, c);
}

static inline uint64_t
ConsoleWriteUnlocked(const char *str)
{
    return KeConDevPutStr(&gConsoleDevice, str);
}

#if __HO_DEBUG_BUILD__
static inline void
ConsoleSyncSerialCursorUnlocked(void)
{
    KeSerialConSinkSyncCursor(&gSerialConsoleSink, gConsoleDevice.CursorX, gConsoleDevice.CursorY);
}
#else
static inline void
ConsoleSyncSerialCursorUnlocked(void)
{
}
#endif

static uint64_t
ConsoleWriteVFmtInternal(const char *fmt, VA_LIST args)
{
    char buf[MAX_FORMAT_BUFFER];
    uint64_t written = 0;

    const char *p;
    for (p = fmt; *p; ++p)
    {
        if (*p != '%')
        {
            (void)ConsoleWriteCharUnlocked(*p);
            written++;
            continue;
        }

        ++p; // Skip '%'

        if (*p == '%')
        {
            (void)ConsoleWriteCharUnlocked(*p);
            written++;
            ++p;
            continue;
        }

        // Parse flags and padding
        BOOL leftAlign = FALSE;
        char pc = 0;

        // Check for left-align flag '-'
        if (*p == '-')
        {
            leftAlign = TRUE;
            ++p;
        }

        if (*p == '0' && !leftAlign)
        {
            pc = '0';
            ++p;
        }
        else if (*p == '0')
        {
            // Skip '0' when left-aligned (use space padding)
            ++p;
        }

        uint32_t width = 0;
        while (*p >= '0' && *p <= '9')
        {
            width = width * 10 + (*p - '0');
            p++;
        }
        // Clamp width to prevent buffer overflow
        if (width >= MAX_FORMAT_BUFFER)
            width = MAX_FORMAT_BUFFER - 1;
        if (width > 0 && !pc)
            pc = ' ';

        switch (*p)
        {
        case 'c': {
            char c = (char)VA_ARG(args, int);
            (void)ConsoleWriteCharUnlocked(c);
            written++;
            break;
        }
        case 's': {
            const char *s = VA_ARG(args, const char *);
            if (s == NULL)
                s = "(null)";
            size_t len = strlen(s);
            size_t padLen = (width > len) ? width - len : 0;
            if (!leftAlign && padLen > 0)
            {
                for (uint32_t i = 0; i < padLen; ++i)
                {
                    (void)ConsoleWriteCharUnlocked(pc);
                    written++;
                }
            }
            written += ConsoleWriteUnlocked(s);
            if (leftAlign && padLen > 0)
            {
                for (uint32_t i = 0; i < padLen; ++i)
                {
                    (void)ConsoleWriteCharUnlocked(' ');
                    written++;
                }
            }
            break;
        }
        case 'd':
        case 'i': {
            int64_t val = VA_ARG(args, int);
            uint64_t numLen;
            if (leftAlign)
            {
                numLen = Int64ToStringEx(val, buf, 0, 0);
                written += ConsoleWriteUnlocked(buf);
                for (uint32_t i = numLen; i < width; ++i)
                {
                    (void)ConsoleWriteCharUnlocked(' ');
                    written++;
                }
            }
            else
            {
                (void)Int64ToStringEx(val, buf, width, pc);
                written += ConsoleWriteUnlocked(buf);
            }
            break;
        }
        case 'l': {
            uint64_t numLen;
            if (*(p + 1) == 'd' || *(p + 1) == 'i') // long decimal
            {
                ++p;
                int64_t val = VA_ARG(args, long);
                if (leftAlign)
                {
                    numLen = Int64ToStringEx(val, buf, 0, 0);
                    written += ConsoleWriteUnlocked(buf);
                    for (uint32_t i = numLen; i < width; ++i)
                    {
                        (void)ConsoleWriteCharUnlocked(' ');
                        written++;
                    }
                }
                else
                {
                    (void)Int64ToStringEx(val, buf, width, pc);
                    written += ConsoleWriteUnlocked(buf);
                }
            }
            else if (*(p + 1) == 'u') // long unsigned
            {
                ++p;
                uint64_t val = VA_ARG(args, unsigned long);
                if (leftAlign)
                {
                    numLen = UInt64ToStringEx(val, buf, 10, 0, 0);
                    written += ConsoleWriteUnlocked(buf);
                    for (uint32_t i = numLen; i < width; ++i)
                    {
                        (void)ConsoleWriteCharUnlocked(' ');
                        written++;
                    }
                }
                else
                {
                    (void)UInt64ToStringEx(val, buf, 10, width, pc);
                    written += ConsoleWriteUnlocked(buf);
                }
            }
            else if (*(p + 1) == 'x' || *(p + 1) == 'X') // long hex
            {
                ++p;
                uint64_t val = VA_ARG(args, unsigned long);
                if (leftAlign)
                {
                    numLen = UInt64ToStringEx(val, buf, 16, 0, 0);
                    written += ConsoleWriteUnlocked(buf);
                    for (uint32_t i = numLen; i < width; ++i)
                    {
                        (void)ConsoleWriteCharUnlocked(' ');
                        written++;
                    }
                }
                else
                {
                    (void)UInt64ToStringEx(val, buf, 16, width, pc);
                    written += ConsoleWriteUnlocked(buf);
                }
            }
            else
            {
                HO_KPANIC(EC_ILLEGAL_ARGUMENT, "Unsupported format in kernel printf");
            }
            break;
        }
        case 'u': {
            uint64_t val = VA_ARG(args, unsigned int);
            uint64_t numLen;
            if (leftAlign)
            {
                numLen = UInt64ToStringEx(val, buf, 10, 0, 0);
                written += ConsoleWriteUnlocked(buf);
                for (uint32_t i = numLen; i < width; ++i)
                {
                    (void)ConsoleWriteCharUnlocked(' ');
                    written++;
                }
            }
            else
            {
                (void)UInt64ToStringEx(val, buf, 10, width, pc);
                written += ConsoleWriteUnlocked(buf);
            }
            break;
        }
        case 'x':
        case 'X': {
            uint64_t val = VA_ARG(args, uint64_t);
            uint64_t numLen;
            if (leftAlign)
            {
                numLen = UInt64ToStringEx(val, buf, 16, 0, 0);
                written += ConsoleWriteUnlocked(buf);
                for (uint32_t i = numLen; i < width; ++i)
                {
                    (void)ConsoleWriteCharUnlocked(' ');
                    written++;
                }
            }
            else
            {
                (void)UInt64ToStringEx(val, buf, 16, width, pc);
                written += ConsoleWriteUnlocked(buf);
            }
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)VA_ARG(args, void *);
            written += ConsoleWriteUnlocked("0X");
            (void)UInt64ToString(val, buf, 16, TRUE);
            written += ConsoleWriteUnlocked(buf);
            break;
        }
        // HimuOS kernel specific placeholders
        case 'k': {
            if (*(p + 1) == 'e') // error codes
            {
                ++p;
                HO_STATUS val = VA_ARG(args, HO_STATUS);
                const char *msg = KrGetStatusMessage(val);
                written += ConsoleWriteUnlocked(msg);
            }
            else if (*(p + 1) == 's') // error codes status (FAIL or SUCCESS)
            {
                ++p;
                HO_STATUS val = VA_ARG(args, HO_STATUS);
                const char *msg = HO_LIKELY(!val) ? "OK" : "FAILED";
                written += ConsoleWriteUnlocked(msg);
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

    return written;
}

HO_PUBLIC_API HO_STATUS
ConsoleInit(KE_VIDEO_DRIVER *driver, BITMAP_FONT_INFO *font)
{
    if (gConsoleInitialized)
        return EC_SUCCESS;
#ifndef __HO_DEBUG_BUILD__
    // In release builds, we only use GfxConSink
    GfxConSinkInit(&gGfxConsoleSink, driver, font, 1);
    ConDevInit(&gConsoleDevice, (CONSOLE_SINK *)&gGfxConsoleSink);
#else
    KeMuxConSinkInit(&gMuxConsoleSink);
    KeGfxConSinkInit(&gGfxConsoleSink, driver, font, 1);
    KeMuxConSinkAddSink(&gMuxConsoleSink, (KE_CONSOLE_SINK *)&gGfxConsoleSink); // As primary sink
    KeSerialConSinkInit(&gSerialConsoleSink, COM1_PORT);
    KeMuxConSinkAddSink(&gMuxConsoleSink, (KE_CONSOLE_SINK *)&gSerialConsoleSink);
    KeConDevInit(&gConsoleDevice, (KE_CONSOLE_SINK *)&gMuxConsoleSink);
#endif
    gConsoleInitialized = TRUE;
    return EC_SUCCESS;
}

HO_PUBLIC_API HO_STATUS
ConsolePromoteAllocatorStorage(void)
{
    if (!gConsoleInitialized)
        return EC_INVALID_STATE;

#if __HO_DEBUG_BUILD__
    return KeMuxConSinkPromoteToAllocator(&gMuxConsoleSink);
#else
    return EC_SUCCESS;
#endif
}

HO_PUBLIC_API KE_CONSOLE_DEVICE *
ConsoleGetGlobalDevice(void)
{
    return &gConsoleDevice;
}

HO_PUBLIC_API int
ConsoleWriteChar(char c)
{
    if (!gConsoleInitialized)
        return -1;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);
    int status = ConsoleWriteCharUnlocked(c);
    ConsoleSyncSerialCursorUnlocked();
    KeLeaveCriticalSection(&criticalSection);
    return status;
}

HO_PUBLIC_API uint64_t
ConsoleWrite(const char *str)
{
    if (!gConsoleInitialized)
        return 0;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);
    uint64_t written = ConsoleWriteUnlocked(str);
    ConsoleSyncSerialCursorUnlocked();
    KeLeaveCriticalSection(&criticalSection);
    return written;
}

HO_PUBLIC_API uint64_t
ConsoleWriteFmt(const char *fmt, ...)
{
    if (!gConsoleInitialized)
        return 0;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);

    VA_LIST args;
    VA_START(args, fmt);
    uint64_t written = ConsoleWriteVFmtInternal(fmt, args);
    VA_END(args);

    ConsoleSyncSerialCursorUnlocked();
    KeLeaveCriticalSection(&criticalSection);
    return written;
}

HO_PUBLIC_API uint64_t
ConsoleWriteVFmt(const char *fmt, VA_LIST args)
{
    if (!gConsoleInitialized)
        return 0;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);
    uint64_t written = ConsoleWriteVFmtInternal(fmt, args);
    ConsoleSyncSerialCursorUnlocked();
    KeLeaveCriticalSection(&criticalSection);
    return written;
}

HO_PUBLIC_API void
ConsoleClearScreen(COLOR32 color)
{
    if (!gConsoleInitialized)
        return;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);
    KeConDevClearScreen(&gConsoleDevice, color);
    ConsoleSyncSerialCursorUnlocked();
    KeLeaveCriticalSection(&criticalSection);
}

HO_PUBLIC_API void
ConsoleFlush(void)
{
    if (!gConsoleInitialized)
        return;

    KE_CRITICAL_SECTION criticalSection = {0};
    KeEnterCriticalSection(&criticalSection);
    ConsoleSyncSerialCursorUnlocked();
#if __HO_DEBUG_BUILD__
    SerialDrain(gSerialConsoleSink.Port);
#endif
    KeLeaveCriticalSection(&criticalSection);
}
