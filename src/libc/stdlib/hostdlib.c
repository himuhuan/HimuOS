#include <hostdlib.h>
#include <stdarg.h>

#define DIGITS_STR          "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define ITOA_32_BUFFER_SIZE 33
#define ITOA_64_BUFFER_SIZE 65

static void WriteIntDecimal(uint64_t num, char *buf, uint8_t digitSize);
static void WriteIntTwoBase(uint64_t num, char *buf, uint8_t digitSize, int base);
static uint8_t CountDecDigit(uint64_t n);
static int FindMostSignificantBit(uint64_t num);
static BOOL IsValidBase(int base);

HO_PUBLIC_API void
ReverseString(char *begin, char *end)
{
    while (begin < end)
    {
        char temp = *begin;
        *begin++ = *end;
        *end-- = temp;
    }
}

HO_PUBLIC_API uint64_t
Int64ToString(int64_t value, char *str, BOOL prefix)
{
    if (prefix)
        return Int64ToStringEx(value, str, -1, '0');
    else
        return Int64ToStringEx(value, str, 0, 0);
}

HO_PUBLIC_API uint64_t
UInt64ToString(uint64_t value, char *str, int base, BOOL prefix)
{
    if (prefix)
        return UInt64ToStringEx(value, str, base, -1, '0');
    else
        return UInt64ToStringEx(value, str, base, 0, 0);
}

HO_PUBLIC_API uint64_t
UInt64ToStringEx(uint64_t value, char *str, int base, int32_t padding, char padChar)
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
        memset(str, padChar, toFill);
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
        memset(str, padChar, toFill);
        totalDigits += toFill;
    }

    *(str + totalDigits) = '\0';
    return totalDigits;
}

HO_PUBLIC_API uint64_t
Int64ToStringEx(int64_t val, char *buf, int32_t padding, char padChar)
{
    uint64_t magnitude = 0;
    uint8_t neg = 0;
    if (val < 0)
    {
        magnitude = (uint64_t)(-(val + 1)) + 1;
        neg = 1;
    }
    else
    {
        magnitude = val;
    }

    uint64_t digits = UInt64ToStringEx(magnitude, buf + neg, 10, padding - neg, padChar);
    if (neg)
    {
        buf[0] = '-';
        digits += 1;
    }
    return digits;
}

//
// static functions
//

static void
WriteIntDecimal(uint64_t num, char *buf, uint8_t digitSize)
{
    char *end = buf + digitSize;
    if (num == 0)
    {
        *(--end) = '0';
        return;
    }
    while (num > 0)
    {
        *(--end) = DIGITS_STR[num % 10];
        num /= 10;
    }
}

static void
WriteIntTwoBase(uint64_t num, char *buf, uint8_t digitSize, int base)
{
    int step = FindMostSignificantBit(base);
    do
    {
        uint8_t digit = (uint8_t)(num & (base - 1));
        buf[--digitSize] = DIGITS_STR[digit];
        num >>= step;
    } while (num != 0);
}

static uint8_t
CountDecDigit(uint64_t n)
{
    if (n < 10)
        return 1;
    if (n < 100)
        return 2;
    if (n < 1000)
        return 3;
    if (n < 10000)
        return 4;
    if (n < 100000)
        return 5;
    if (n < 1000000)
        return 6;
    if (n < 10000000)
        return 7;
    if (n < 100000000)
        return 8;
    if (n < 1000000000)
        return 9;
    if (n < 10000000000)
        return 10;
    if (n < 100000000000)
        return 11;
    if (n < 1000000000000)
        return 12;
    if (n < 10000000000000)
        return 13;
    if (n < 100000000000000)
        return 14;
    if (n < 1000000000000000)
        return 15;
    if (n < 10000000000000000)
        return 16;
    if (n < 100000000000000000)
        return 17;
    if (n < 1000000000000000000)
        return 18;
    if (n < 10000000000000000000ull)
        return 19;
    return 20;
}

static int
FindMostSignificantBit(uint64_t num)
{
    if (!num)
        return -1;
    return 63 - __builtin_clzll(num);
}

static BOOL
IsValidBase(int base)
{
    if (base == 2 || base == 4 || base == 8 || base == 10 || base == 16 || base == 32)
        return TRUE;
    return FALSE;
}