/**
 * HIMU OPERATING SYSTEM
 *
 * File: string.h
 * Stanard string & memory functions
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __LIB_STDSTRING_H
#define __LIB_STDSTRING_H 1

#include "stddef.h"

/* Memory manipulation */

void *memset(void *dst, int ch, size_t siz);
void *memcpy(void *dst, const void *src, size_t siz);
int memcmp(const void *lhs, const void *rhs, size_t count);

/* String manipulation */

char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);

/* String examination */

size_t strlen(const char *str);
int strcmp(const char *lhs, const char *rhs);
char *strchr(const char *str, int ch);
char *strrchr(const char *str, int ch);

#endif /* ^^ __LIB_STDSTRING_H ^^ */