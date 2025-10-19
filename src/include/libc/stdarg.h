/**
 * HimuOperatingSystem
 *
 * File: stdarg.h
 * Description: Standard variadic arguments support
 * Copyright(c) 2024-2025 HimuOperatingSystem libc, All rights reserved.
 */

#pragma once

typedef __builtin_va_list va_list;

#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v)      __builtin_va_end(v)
#define va_arg(v, l)   __builtin_va_arg(v, l)
#define va_copy(d, s)  __builtin_va_copy(d, s)

#ifndef __NO_HOEXTS

typedef va_list VA_LIST;
#define VA_START(v, l) va_start(v, l)
#define VA_END(v)      va_end(v)
#define VA_ARG(v, l)   va_arg(v, l)
#define VA_COPY(d, s)  va_copy(d, s)

#endif // __NO_HOEXTS
