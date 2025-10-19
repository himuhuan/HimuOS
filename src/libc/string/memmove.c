#include "libc/string.h"

void *
memmove(void *dst, const void *src, size_t n)
{
    size_t i;
    uint8_t *p = dst, *q = (uint8_t *)src;

    // maybe better pipeline?
    if (src >= dst || src + n <= dst)
    {
        for (i = 0; i < n; ++i)
            *p++ = *q++;
    }
    else
    {
        p += n;
        q += n;
        while (n-- > 0)
            *--p = *--q;
    }

    return dst;
}
