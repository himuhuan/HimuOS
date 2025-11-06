#include <hostdlib.h>
#include <stdarg.h>

#define DIGITS_STR          "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
#define ITOA_32_BUFFER_SIZE 33
#define ITOA_64_BUFFER_SIZE 65

static inline size_t TryCopy(char *buffer, size_t len, size_t current, const char *src, size_t n);

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
Int32ToString(int32_t value, char *str, int base, BOOL prefix)
{
    if (str == NULL || base < 2 || base > 36)
        return 0;

    if (value < 0 && base == 10)
    {
        *str++ = '-';
        return UInt32ToString((uint32_t)-value, str, base, prefix) + 1;
    }
    else
    {
        return UInt32ToString((uint32_t)value, str, base, prefix);
    }
}

HO_PUBLIC_API uint64_t
Int64ToString(int64_t value, char *str, int base, BOOL prefix)
{
    if (str == NULL || base < 2 || base > 36)
        return 0;

    if (value < 0 && base == 10)
    {
        *str++ = '-';
        return UInt64ToString((uint64_t)-value, str, base, prefix) + 1;
    }
    else
    {
        return UInt64ToString((uint64_t)value, str, base, prefix);
    }
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
        return 0;
    }
}

HO_PUBLIC_API uint64_t
UInt32ToString(uint32_t value, char *str, int base, BOOL prefix)
{
    char *currentPosition = str;
    if (str == NULL || base < 2 || base > 36)
    {
        return 0;
    }

    if (value == 0)
    {
        *currentPosition++ = '0';
        *currentPosition = '\0';
        return 1;
    }

    while (value != 0)
    {
        uint32_t remainder = value % base;
        *currentPosition++ = DIGITS_STR[remainder];
        value = value / base;
    }
    *currentPosition = '\0';

    uint64_t len = (uint64_t)(currentPosition - str);
    ReverseString(str, currentPosition - 1);

    if (prefix && _IsPowerOfTwo(base))
    {
        int bits = _BitsPerDigit(base);
        if (bits > 0)
        {
            int totalBits = 32;
            int digitsNeeded = (totalBits + bits - 1) / bits;

            int currentLen = (int)len;
            if (currentLen < digitsNeeded)
            {
                int pad = digitsNeeded - currentLen;
                for (int i = currentLen; i >= 0; --i)
                    str[i + pad] = str[i];
                for (int i = 0; i < pad; ++i)
                    str[i] = '0';

                len = (uint64_t)digitsNeeded;
            }
        }
    }

    return len;
}

HO_PUBLIC_API uint64_t
UInt64ToString(uint64_t value, char *str, int base, BOOL prefix)
{
    char *currentPosition = str;
    if (str == NULL || base < 2 || base > 36)
    {
        return 0;
    }

    if (value == 0)
    {
        *currentPosition++ = '0';
    }
    else
    {
        while (value != 0)
        {
            uint64_t remainder = value % base;
            *currentPosition++ = DIGITS_STR[remainder];
            value = value / base;
        }
    }
    *currentPosition = '\0';

    uint64_t len = (uint64_t)(currentPosition - str);
    ReverseString(str, currentPosition - 1);

    if (prefix && _IsPowerOfTwo(base))
    {
        int bits = _BitsPerDigit(base);
        if (bits > 0)
        {
            int totalBits = 64;
            int digitsNeeded = (totalBits + bits - 1) / bits;

            int currentLen = (int)len;
            if (currentLen < digitsNeeded)
            {
                int pad = digitsNeeded - currentLen;
                for (int i = currentLen; i >= 0; --i)
                    str[i + pad] = str[i];
                for (int i = 0; i < pad; ++i)
                    str[i] = '0';
                len = (uint64_t)digitsNeeded;
            }
        }
    }

    return len;
}

#define WRITE_CHAR(ch)                                                                                                 \
    do                                                                                                                 \
    {                                                                                                                  \
        if (len > 0 && written < (len - 1))                                                                            \
        {                                                                                                              \
            buffer[written] = (char)(ch);                                                                              \
            written++;                                                                                                 \
        }                                                                                                              \
        needed++;                                                                                                      \
    } while (0)

HO_PUBLIC_API size_t
FormatString(char *buffer, size_t len, const char *format, ...)
{

    VA_LIST args;
    VA_START(args, format);
    size_t written = 0, needed = 0;

    const char *p = format;
    while (*p)
    {
        if (*p != '%')
        {
            WRITE_CHAR(*p);
            ++p;
            continue;
        }
        ++p; // Skip '%'
        switch (*p)
        {
        case 'c': {
            char c = (char)VA_ARG(args, int);
            WRITE_CHAR((char)c);
            break;
        }
        case 's': {
            const char *s = VA_ARG(args, const char *);
            if (s == NULL)
                s = "(null)";
            size_t n = strlen(s);
            size_t copied = TryCopy(buffer, len, written, s, n);
            written += copied;
            needed += n;
            break;
        }
        case 'd':
        case 'i': {
            int val = VA_ARG(args, int);
            char temp[ITOA_32_BUFFER_SIZE];
            size_t n = (size_t)Int32ToString(val, temp, 10, FALSE);
            size_t copied = TryCopy(buffer, len, written, temp, n);
            written += copied;
            needed += n;
            break;
        }
        case 'u': {
            uint32_t val = VA_ARG(args, uint32_t);
            char temp[ITOA_32_BUFFER_SIZE];
            size_t n = (size_t)UInt32ToString(val, temp, 10, FALSE);
            size_t copied = TryCopy(buffer, len, written, temp, n);
            written += copied;
            needed += n;
            break;
        }
        // WARNING: HimuOS-specific: 'x' and 'X' behave the same way
        // and always convert to uint64_t
        case 'x':
        case 'X': {
            uint64_t val = VA_ARG(args, uint64_t);
            char temp[ITOA_64_BUFFER_SIZE];
            size_t n = (size_t)UInt64ToString(val, temp, 16, FALSE);
            size_t copied = TryCopy(buffer, len, written, temp, n);
            written += copied;
            needed += n;
            break;
        }
        case '%': {
            WRITE_CHAR('%');
            break;
        }
        case 'p': {
            void *ptr = VA_ARG(args, void *);
            uint64_t val = (uint64_t)ptr;
            WRITE_CHAR('0');
            WRITE_CHAR('X');
            char temp[ITOA_64_BUFFER_SIZE];
            size_t n = (size_t)UInt64ToString(val, temp, 16, TRUE);
            written += TryCopy(buffer, len, written, temp, n);
            needed += n;
            break;
        }
        default:
            WRITE_CHAR('%');
            if (*p)
                WRITE_CHAR(*p);
            break;
        }
        ++p;
    }

    if (len > 0)
    {
        size_t nullPos = (written < len) ? written : (len - 1);
        buffer[nullPos] = '\0';
    }

    VA_END(args);
    return needed;
}

#undef WRITE_CHAR

static inline size_t
TryCopy(char *buffer, size_t len, size_t current, const char *src, size_t n)
{
    if (len > 0 && current < len - 1)
    {
        size_t available = len - 1 - current;
        size_t toCopy = (n < available) ? n : available;
        memcpy(&buffer[current], src, toCopy * sizeof(char));
        return toCopy;
    }
    return 0;
}