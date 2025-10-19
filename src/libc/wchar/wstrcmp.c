#include "libc/wchar.h"

int
wstrcmp(const wchar_t *s1, const wchar_t *s2)
{
    while (*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return (int)(*s1 - *s2);
}
