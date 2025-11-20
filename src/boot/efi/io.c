#include "io.h"
#include "libc/string.h"
#include <libc/stdarg.h>
#include <libc/hostdlib.h>

#define CONSOLE_BUFFER_SIZE 256
#define DIGITS_STR          L"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

static const CHAR16 *EfiStatusToString(EFI_STATUS status);
static void FlushConsoleBuffer(CHAR16 *Buffer, UINTN *Index);
static void ConsoleEmitChar(CHAR16 *Buffer, UINTN *Index, CHAR16 Char);
static void ConsoleEmitString(CHAR16 *Buffer, UINTN *Index, const CHAR16 *Str);
static void WriteIntDecimal(UINT64 num, CHAR16 *buf, UINT8 digitSize);
static void WriteIntTwoBase(UINT64 num, CHAR16 *buf, UINT8 digitSize, int base);
static UINT64 UInt64ToWideStringEx(UINT64 value, CHAR16 *str, int base, int padding, CHAR16 padChar);
static UINT64 Int64ToWideStringEx(INT64 val, CHAR16 *buf, int padding, CHAR16 padChar);

INT64
ConsoleReadline(OUT CHAR16 *Buffer, IN INT64 MaxCount)
{
    struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *conin = g_ST->ConsoleInput;
    struct EFI_INPUT_KEY key;
    UINT64 eventidx;
    INT64 i = 0;
    enum error_code
    {
        OK,
        EOL,
        FAILED
    } code = OK;

    memset(Buffer, 0, sizeof(Buffer[0]) * MaxCount);

    while (i < MaxCount - 1 && code == OK)
    {
        g_ST->BootServices->WaitForEvent(1, &conin->WaitForKey, &eventidx);
        if (conin->ReadKeyStroke(conin, &key) != EFI_SUCCESS)
        {
            code = FAILED;
            break;
        }

        CHAR16 ch = key.UnicodeChar;

        if (ch == TEXT('\r') || ch == TEXT('\n'))
        {
            ConsoleWriteStr(TEXT("\r\n"));
            code = EOL;
            break;
        }

        if (key.ScanCode == TEXT('\b'))
        {
            if (i > 0)
            {
                i--;
                Buffer[i] = 0;
                ConsoleWriteStr(TEXT("\b \b"));
            }
            continue;
        }

        if (ch >= 0x20 && ch <= 0x7E)
        {
            Buffer[i++] = ch;
            CHAR16 echo[2] = {ch, 0};
            ConsoleWriteStr(echo);
        }
    }

    Buffer[i] = 0;
    return (code == FAILED) ? -1 : i;
}

void
ConsoleWriteStr(IN const CHAR16 *Buffer)
{
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *conout = g_ST->ConsoleOutput;
    conout->OutputString(conout, (CHAR16 *)Buffer);
}

EFI_STATUS
ConsoleFormatWrite(const CHAR16 *fmt, ...)
{
    CHAR16 buffer[CONSOLE_BUFFER_SIZE];
    UINTN index = 0;
    CHAR16 numBuffer[64];

    va_list args;
    va_start(args, fmt);

    while (*fmt)
    {
        if (*fmt != L'%')
        {
            ConsoleEmitChar(buffer, &index, *fmt);
            fmt++;
            continue;
        }
        fmt++;

        if (*fmt == L'%')
        {
            (void)ConsoleEmitChar(buffer, &index, *fmt);
            ++fmt;
            continue;
        }

        // Parse padding
        CHAR16 pc = ' ';
        if (*fmt == L'0')
        {
            pc = L'0';
            ++fmt;
        }
        UINT32 width = 0;
        while (*fmt >= L'0' && *fmt <= L'9')
        {
            width = width * 10 + (*fmt - L'0');
            fmt++;
        }
        if (width > 0 && !pc)
            pc = L' ';

        switch (*fmt)
        {
        case L's': {
            CHAR16 *strArgs = va_arg(args, CHAR16 *);
            ConsoleEmitString(buffer, &index, strArgs);
            break;
        }
        case L'd': {
            int64_t val = va_arg(args, int64_t);
            Int64ToWideStringEx(val, numBuffer, width, pc);
            ConsoleEmitString(buffer, &index, numBuffer);
            break;
        }
        case L'u': {
            uint64_t val = va_arg(args, uint64_t);
            UInt64ToWideStringEx(val, numBuffer, 10, width, pc);
            ConsoleEmitString(buffer, &index, numBuffer);
            break;
        }
        case L'x': {
            uint64_t val = va_arg(args, uint64_t);
            UInt64ToWideStringEx(val, numBuffer, 16, width, pc);
            ConsoleEmitString(buffer, &index, numBuffer);
            break;
        }
        case L'p': {
            uint64_t val = (uint64_t)(UINTN)va_arg(args, void *);
            ConsoleEmitString(buffer, &index, L"0X");
            UInt64ToWideStringEx(val, numBuffer, 16, -1, L'0');
            ConsoleEmitString(buffer, &index, numBuffer);
            break;
        }
        case L'k': { // Custom: EFI_STATUS
            EFI_STATUS st = va_arg(args, EFI_STATUS);
            ConsoleEmitString(buffer, &index, EfiStatusToString(st));
            break;
        }
        case L'%': {
            ConsoleEmitChar(buffer, &index, L'%');
            break;
        }
        default: {
            ConsoleEmitChar(buffer, &index, *fmt);
            break;
        }
        }
        if (*fmt)
            fmt++;
    }
    FlushConsoleBuffer(buffer, &index);

    va_end(args);
    return EFI_SUCCESS;
}

//
// static functions
//

static void
FlushConsoleBuffer(CHAR16 *Buffer, UINTN *Index)
{
    if (*Index > 0)
    {
        Buffer[*Index] = L'\0';
        g_ST->ConsoleOutput->OutputString(g_ST->ConsoleOutput, Buffer);
        *Index = 0;
    }
}

static void
ConsoleEmitChar(CHAR16 *Buffer, UINTN *Index, CHAR16 Char)
{
    if (*Index >= CONSOLE_BUFFER_SIZE - 1)
    {
        FlushConsoleBuffer(Buffer, Index);
    }
    Buffer[(*Index)++] = Char;
}

static void
ConsoleEmitString(CHAR16 *Buffer, UINTN *Index, const CHAR16 *Str)
{
    if (!Str)
    {
        Str = L"(null)"; // 处理空指针
    }
    while (*Str)
    {
        ConsoleEmitChar(Buffer, Index, *Str);
        Str++;
    }
}

static void
WriteIntDecimal(UINT64 num, CHAR16 *buf, UINT8 digitSize)
{
    CHAR16 *end = buf + digitSize;
    if (num == 0)
    {
        *(--end) = L'0';
        return;
    }
    while (num > 0)
    {
        *(--end) = DIGITS_STR[num % 10];
        num /= 10;
    }
}

static void
WriteIntTwoBase(UINT64 num, CHAR16 *buf, UINT8 digitSize, int base)
{
    int step = FindMostSignificantBit(base);
    do
    {
        uint8_t digit = (uint8_t)(num & (base - 1));
        buf[--digitSize] = DIGITS_STR[digit];
        num >>= step;
    } while (num != 0);
}

static UINT64
UInt64ToWideStringEx(UINT64 value, CHAR16 *str, int base, int padding, CHAR16 padChar)
{
    if (str == NULL || !IsValidBase(base))
        return 0;

    int totalDigits = 0;
    if (base == 10)
    {
        totalDigits = CountDecDigit(value);
        int toFill = 0;
        if (padding > totalDigits)
            toFill = padding - totalDigits;
        WriteIntDecimal(value, str + toFill, totalDigits);
        for (int i = 0; i < toFill; i++)
            *(str + i) = padChar;
        totalDigits += toFill;
    }
    else
    {
        int toFill = 0;
        int bitsPerDigit = FindMostSignificantBit(base);

        if (value == 0)
            totalDigits = 1;
        else
        {
            int bitsInValue = FindMostSignificantBit(value) + 1;
            totalDigits = (bitsInValue + bitsPerDigit - 1) / bitsPerDigit;
        }

        // special case for hex with negative padding means align to full digits
        if (padding < 0)
            padding = (64 + bitsPerDigit - 1) / bitsPerDigit;
        if (padding > totalDigits)
            toFill = padding - totalDigits;

        WriteIntTwoBase(value, str + toFill, totalDigits, base);
        for (int i = 0; i < toFill; i++)
            *(str + i) = padChar;
        totalDigits += toFill;
    }

    *(str + totalDigits) = L'\0';
    return totalDigits;
}

static UINT64
Int64ToWideStringEx(INT64 val, CHAR16 *buf, int padding, CHAR16 padChar)
{
    UINT64 magnitude = 0;
    UINT8 neg = 0;
    if (val < 0)
    {
        magnitude = (uint64_t)(-(val + 1)) + 1;
        neg = 1;
    }
    else
    {
        magnitude = val;
    }

    UINT64 digits = UInt64ToWideStringEx(magnitude, buf + neg, 10, padding - neg, padChar);
    if (neg)
    {
        buf[0] = '-';
        digits += 1;
    }
    return digits;
}

static const CHAR16 *
EfiStatusToString(EFI_STATUS status)
{
    switch (status)
    {
    case EFI_SUCCESS:
        return L"EFI_SUCCESS";
    case EFI_LOAD_ERROR:
        return L"EFI_LOAD_ERROR";
    case EFI_INVALID_PARAMETER:
        return L"EFI_INVALID_PARAMETER";
    case EFI_UNSUPPORTED:
        return L"EFI_UNSUPPORTED";
    case EFI_BUFFER_TOO_SMALL:
        return L"EFI_BUFFER_TOO_SMALL";
    case EFI_DEVICE_ERROR:
        return L"EFI_DEVICE_ERROR";
    case EFI_OUT_OF_RESOURCES:
        return L"EFI_OUT_OF_RESOURCES";
    default:
        return L"EFI_UNKNOWN_ERROR";
    }
}
