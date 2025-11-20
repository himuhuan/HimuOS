/**
 * HimuOperatingSystem
 *
 * File: hostdlib.h
 * Description: HimuOS stdlib extended utilities.
 * Copyright(c) 2024-2025 HimuOperatingSystem, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

HO_PUBLIC_API void ReverseString(char *begin, char *end);

HO_PUBLIC_API uint64_t Int64ToString(int64_t value, char *out, BOOL prefix);

HO_PUBLIC_API uint64_t UInt64ToString(uint64_t value, char *out, int base, BOOL prefix);

HO_PUBLIC_API uint64_t UInt64ToStringEx(uint64_t value, char *str, int base, int32_t padding, char padChar);

HO_PUBLIC_API uint64_t Int64ToStringEx(int64_t val, char *buf, int32_t padding, char padChar);

HO_PUBLIC_API uint8_t CountDecDigit(uint64_t n);

HO_PUBLIC_API int FindMostSignificantBit(uint64_t num);

HO_PUBLIC_API BOOL IsValidBase(int base);

/**
 * FormatString: Formats a string into the provided buffer supporting various format specifiers (including some
 * kernel-extended ones).
 * @param buffer: The buffer to write the formatted string into.
 * @param len: The length of the buffer.
 * @param format: The format string.
 * @param ...: Additional arguments to format.
 * @returns: The length of the formatted string actually required (not including the null terminator).
 */
// HO_PUBLIC_API size_t FormatString(char *buffer, size_t len, const char *format, ...);