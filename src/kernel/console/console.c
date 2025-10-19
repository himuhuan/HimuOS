#include <kernel/console.h>
#include <string.h>
#include <hostdlib.h>
#include <stdarg.h>

#define MAX_FORMAT_BUFFER 21

static CONSOLE kKrnlConsole;

HO_KERNEL_API void
ConInit(void)
{
    memset(&kKrnlConsole, 0, sizeof(kKrnlConsole));
}

HO_KERNEL_API void
ConAddSink(CONSOLE_SINK *sink)
{
    if (kKrnlConsole.SinkCnt >= MAX_CONSOLE_SINKS)
        return;
    kKrnlConsole.Sinks[kKrnlConsole.SinkCnt] = sink;
    kKrnlConsole.SinkCnt++;
}

HO_KERNEL_API MAYBE_UNUSED void
ConRemoveSink(const char *name)
{
    for (uint8_t i = 0; i < kKrnlConsole.SinkCnt; i++)
    {
        if (strcmp(kKrnlConsole.Sinks[i]->Name, name) == 0)
        {
            // Shift remaining sinks left
            for (uint8_t j = i; j < kKrnlConsole.SinkCnt - 1; j++)
            {
                kKrnlConsole.Sinks[j] = kKrnlConsole.Sinks[j + 1];
            }
            kKrnlConsole.SinkCnt--;
            return;
        }
    }
}

/**
 * ConPutChar - Put a character to all console sinks.
 * @return The character written as an unsigned char cast to an int or EOF on error.
 */
HO_KERNEL_API int
ConPutChar(int c)
{
    int result = c;
    for (uint8_t i = 0; i < kKrnlConsole.SinkCnt; i++)
    {
        if (kKrnlConsole.Sinks[i]->PutChar)
        {
            int res = kKrnlConsole.Sinks[i]->PutChar(kKrnlConsole.Sinks[i], c);
            if (res == EOF)
                result = EOF; // If any sink fails, return EOF
        }
    }
    return result;
}

/**
 * @brief Outputs a null-terminated string to the console.
 *
 * This function writes the provided string to the system console.
 *
 * @param s Pointer to a null-terminated string to be displayed.
 * @return The number of characters written as a 64-bit unsigned integer.
 */
HO_KERNEL_API uint64_t
ConPutStr(const char *s)
{
    uint64_t count = 0;
    while (*s)
    {
        if (ConPutChar(*s) == EOF)
            break; // Stop on error
        s++;
        count++;
    }
    return count;
}

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
HO_KERNEL_API uint64_t
ConPutStrN(const char *s, uint64_t nc)
{
    uint64_t count = 0;
    while (*s && count < nc)
    {
        if (ConPutChar(*s) == EOF)
            break; // Stop on error
        s++;
        count++;
    }
    return count;
}

HO_KERNEL_API uint64_t
ConFormatPrint(const char *fmt, ...)
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
            (void)ConPutChar(*p);
            written++;
            continue;
        }

        ++p; // Skip '%'
        switch (*p)
        {
        case 'c': {
            char c = (char)VA_ARG(args, int);
            (void)ConPutChar(c);
            written++;
            break;
        }
        case 's': {
            const char *s = VA_ARG(args, const char *);
            written += ConPutStr(s);
            break;
        }
        case 'd':
        case 'i': {
            int val = VA_ARG(args, int);
            (void)Int32ToString(val, buf, 10, FALSE);
            written += ConPutStr(buf);
            break;
        }
        case 'u': {
            unsigned int val = VA_ARG(args, unsigned int);
            (void)UInt32ToString(val, buf, 10, FALSE);
            written += ConPutStr(buf);
            break;
        }
        case 'x':
        case 'X': {
            unsigned int val = VA_ARG(args, unsigned int);
            (void)UInt32ToString(val, buf, 16, FALSE);
            written += ConPutStr(buf);
            break;
        }
        case 'p': {
            uint64_t val = (uint64_t)VA_ARG(args, void *);
            written += ConPutStr("0x");
            (void)UInt64ToString(val, buf, 16, TRUE);
            written += ConPutStr(buf);
            break;
        }
        default:
            (void)ConPutChar('%');
            (void)ConPutChar(*p);
            written++;
            break;
        }
    }
    VA_END(args);
    return written;
}

HO_KERNEL_API HO_NODISCARD CONSOLE_SINK *
ConGetSinkByName(const char *name)
{
    for (uint8_t i = 0; i < kKrnlConsole.SinkCnt; i++)
    {
        if (strcmp(kKrnlConsole.Sinks[i]->Name, name) == 0)
        {
            return kKrnlConsole.Sinks[i];
        }
    }
    return NULL;
}

