#include "io.h"
#include "libc/string.h"

void
FormatHex(uint64_t value, CHAR16 *buffer)
{
    int i;
    buffer[0] = '0';
    buffer[1] = 'X';
    for (i = 0; i < 16; ++i)
    {
        int shift = (15 - i) * 4;
        int digit = (value >> shift) & 0xF;
        buffer[2 + i] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
    }
    buffer[18] = 0;
}

void
FormatUInt64(uint64_t value, CHAR16 *buffer)
{
    int i = 19;
    buffer[20] = 0;

    if (value == 0)
    {
        buffer[0] = '0';
        buffer[1] = 0;
        return;
    }

    while (value > 0)
    {
        buffer[i] = '0' + (value % 10);
        value /= 10;
        i--;
    }

    int len = 19 - i;
    for (int j = 0; j <= len; j++)
    {
        buffer[j] = buffer[i + 1 + j];
    }
}

void
FormatAsStorageUnit(uint64_t Size, CHAR16 *buffer)
{
    const CHAR16 *units[] = {TEXT("B"), TEXT("KB"), TEXT("MB"), TEXT("GB"), TEXT("TB")};
    int unit_idx = 0;
    uint64_t value = Size;

    while (value >= 1024 && unit_idx < 4)
    {
        value /= 1024;
        unit_idx++;
    }

    CHAR16 numbuf[21];
    FormatUInt64(value, numbuf);

    int pos = 0;
    while (numbuf[pos])
    {
        buffer[pos] = numbuf[pos];
        pos++;
    }
    buffer[pos++] = ' ';
    int u = 0;
    while (units[unit_idx][u])
    {
        buffer[pos++] = units[unit_idx][u++];
    }
    buffer[pos] = 0;
}

int
CopyString(CHAR16 *dest, const CHAR16 *src)
{
    int len = 0;

    while (src[len])
    {
        dest[len] = src[len];
        len++;
    }
    dest[len] = 0;
    return len;
}

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

void
ConsoleWriteHex(uint64_t value)
{
    CHAR16 hex_buffer[19];
    FormatHex(value, hex_buffer);
    ConsoleWriteStr(hex_buffer);
}

void
ConsoleWriteUInt64(uint64_t value)
{
    CHAR16 buffer[21];
    FormatUInt64(value, buffer);
    ConsoleWriteStr(buffer);
}

void
PrintFormatAddressRange(IN const CHAR16 *Msg, IN uint64_t Begin, IN uint64_t Size)
{
    CHAR16 buffer[256];
    CHAR16 hex_begin[19], hex_end[19], size_str[21];
    int pos = 0;

    FormatHex(Begin, hex_begin);
    FormatHex(Begin + Size - 1, hex_end);
    FormatAsStorageUnit(Size, size_str);

    pos += CopyString(&buffer[pos], Msg);
    pos += CopyString(&buffer[pos], TEXT(": ["));
    pos += CopyString(&buffer[pos], hex_begin);
    pos += CopyString(&buffer[pos], TEXT("] - ["));
    pos += CopyString(&buffer[pos], hex_end);
    pos += CopyString(&buffer[pos], TEXT("] (size: "));
    pos += CopyString(&buffer[pos], size_str);
    pos += CopyString(&buffer[pos], TEXT(")\r\n"));

    buffer[pos] = 0;

    ConsoleWriteStr(buffer);
}

void
PrintFormatStorageSize(IN uint64_t Size)
{
    CHAR16 buffer[21];
    FormatAsStorageUnit(Size, buffer);
    ConsoleWriteStr(buffer);
}
