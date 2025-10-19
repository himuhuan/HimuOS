/**
 * HimuOperatingSystem
 *
 * File: string.h
 * Description: This header provides functions for handling null-terminated byte strings.
 * Created: 2025-07-25 00:29:06 LiuHuan
 * @Last Modified by: LiuHuan
 * @Last Modified time: 2025-07-26 18:10:34
 * Copyright(c) 2024-2025 HimuOperatingSystem, all rights reserved.
 */

#pragma once

#ifndef LIB_STD_STRING_H
#define LIB_STD_STRING_H

#include "stddef.h"
#include "_stdbase.h"

void *memset(void *ptr, int value, size_t num);

int strcmp(const char *a, const char *b);

char *strcpy(char *dest, const char *src);

void *memcpy(void *dest, const void *src, size_t n);

void *memmove(void *dst, const void *src, size_t n);

size_t strlen(const char *str);

#endif // LIB_STD_STRING_H
