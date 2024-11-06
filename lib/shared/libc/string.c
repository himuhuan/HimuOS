/**
 * HIMU OPERATING SYSTEM
 *
 * File: string.c
 * Stanard string & memory functions
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "string.h"

#include "kernel/krnldbg.h"

/* Memory manipulation */

void *memset(void *dst, int ch, size_t len) {
    KASSERT(dst != NULL);
    register unsigned char *ptr = (unsigned char *)dst;
    while (len-- > 0)
        *ptr++ = ch;
    return dst;
}

void *memcpy(void *dest, const void *src, size_t len) {
    KASSERT(dest != NULL && src != NULL);
    if (dest < src) {
        const char *firsts = (const char *)src;
        char *firstd = (char *)dest;
        while (len--)
            *firstd++ = *firsts++;
    } else {
        const char *lasts = (const char *)src + (len - 1);
        char *lastd = (char *)dest + (len - 1);
        while (len--)
            *lastd-- = *lasts--;
    }
    return dest;
}

int memcmp(const void *str1, const void *str2, size_t count) {
    register const unsigned char *s1 = (const unsigned char *)str1;
    register const unsigned char *s2 = (const unsigned char *)str2;

    while (count-- > 0) {
        if (*s1++ != *s2++)
            return s1[-1] < s2[-1] ? -1 : 1;
    }
    return 0;
}

/* String manipulation */

char *strcpy(char *dest, const char *src) {
    KASSERT(dest != NULL && src != NULL);
    char *p = dest;
    while ((*dest++ = *src++))
        continue;
    return p;
}

char *strcat(char *dest, const char *src) {
    KASSERT(dest != NULL && src != NULL);
    char *p = dest;
    while (*p++)
        continue;
    --p;
    while ((*p++ = *src++))
        continue;
    return p;
}

/* String examination */

size_t strlen(const char *str) {
    KASSERT(str != NULL);
    size_t cnt = 0;
    while (*str++) {
        ++cnt;
    }
    return cnt;
}

int strcmp(const char *lhs, const char *rhs) {
    register unsigned char c, d;

    KASSERT(lhs != NULL && rhs != NULL);
    while (1) {
        c = (unsigned char)*lhs++, d = (unsigned char)*rhs++;
        if (c != d)
            return c - d;
        if (c == 0)
            return 0;
    }
    return 0;
}

char *strchr(const char *s, int c) {
    do {
        if (*s == c) {
            return (char *)s;
        }
    } while (*s++);
    return 0;
}

char *strrchr(const char *s, int c) {
    char *rtnval = 0;

    do {
        if (*s == c)
            rtnval = (char *)s;
    } while (*s++);
    return (rtnval);
}