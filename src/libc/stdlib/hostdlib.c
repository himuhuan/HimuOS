#include <hostdlib.h>

#define DIGITS_STR "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

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

HO_PUBLIC_API char *
Int32ToString(int32_t value, char *str, int base, BOOL prefix)
{
    char *ret = str;
    if (str == NULL || base < 2 || base > 36)
        return NULL;
    if (value < 0 && base == 10)
    {
        *str++ = '-';
        UInt32ToString((uint32_t)-value, str, base, prefix);
    }
    else
    {
        UInt32ToString((uint32_t)value, str, base, prefix);
    }
    return ret;
}

HO_PUBLIC_API char *
Int64ToString(int64_t value, char *str, int base, BOOL prefix)
{
    char *ret = str;
    if (str == NULL || base < 2 || base > 36)
        return NULL;
    if (value < 0 && base == 10)
    {
        *str++ = '-';
        UInt64ToString((uint64_t)-value, str, base, prefix);
    }
    else
    {
        UInt64ToString((uint64_t)value, str, base, prefix);
    }
    return ret;
}

static int
_IsPowerOfTwo(int base)
{
    return (base & (base - 1)) == 0;
}

static int
_BitsPerDigit(int base)
{
    switch (base)
    {
    case 2:
        return 1;
    case 4:
        return 2;
    case 8:
        return 3;
    case 16:
        return 4;
    case 32:
        return 5;
    default:
        return 0; // not supported for fixed padding
    }
}

HO_PUBLIC_API char *
UInt32ToString(uint32_t value, char *str, int base, BOOL prefix)
{
    char *currentPosition = str;
    if (str == NULL || base < 2 || base > 36)
    {
        return NULL;
    }
    if (value == 0)
    {
        *currentPosition++ = '0';
        *currentPosition = '\0';
    }
    else
    {
        while (value != 0)
        {
            uint32_t remainder = value % base;
            *currentPosition++ = DIGITS_STR[remainder];
            value = value / base;
        }
        *currentPosition = '\0';
        ReverseString(str, currentPosition - 1);
    }

    if (prefix && _IsPowerOfTwo(base))
    {
        int bits = _BitsPerDigit(base);
        if (bits > 0)
        {
            int totalBits = 32;
            int digitsNeeded = (totalBits + bits - 1) / bits; // full word digits
            int len = 0;
            while (str[len] != '\0')
                len++;
            if (len < digitsNeeded)
            {
                int pad = digitsNeeded - len;
                // shift existing characters right
                for (int i = len; i >= 0; --i)
                    str[i + pad] = str[i];
                for (int i = 0; i < pad; ++i)
                    str[i] = '0';
            }
        }
    }

    return str;
}

HO_PUBLIC_API char *
UInt64ToString(uint64_t value, char *str, int base, BOOL prefix)
{
    char *currentPosition = str;
    if (str == NULL || base < 2 || base > 36)
    {
        return NULL;
    }
    if (value == 0)
    {
        *currentPosition++ = '0';
        *currentPosition = '\0';
    }
    else
    {
        while (value != 0)
        {
            uint64_t remainder = value % base;
            *currentPosition++ = DIGITS_STR[remainder];
            value = value / base;
        }
        *currentPosition = '\0';
        ReverseString(str, currentPosition - 1);
    }

    if (prefix && _IsPowerOfTwo(base))
    {
        int bits = _BitsPerDigit(base);
        if (bits > 0)
        {
            int totalBits = 64;
            int digitsNeeded = (totalBits + bits - 1) / bits; // full word digits
            int len = 0;
            while (str[len] != '\0')
                len++;
            if (len < digitsNeeded)
            {
                int pad = digitsNeeded - len;
                for (int i = len; i >= 0; --i)
                    str[i + pad] = str[i];
                for (int i = 0; i < pad; ++i)
                    str[i] = '0';
            }
        }
    }

    return str;
}