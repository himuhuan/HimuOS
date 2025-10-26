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

HO_PUBLIC_API uint64_t Int32ToString(int32_t value, char *out, int base, BOOL prefix);

HO_PUBLIC_API uint64_t Int64ToString(int64_t value, char *out, int base, BOOL prefix);

HO_PUBLIC_API uint64_t UInt32ToString(uint32_t value, char *out, int base, BOOL prefix);

HO_PUBLIC_API uint64_t UInt64ToString(uint64_t value, char *out, int base, BOOL prefix);

/**
 * FormatString: Formats a string into the provided buffer supporting various format specifiers (including some
 * kernel-extended ones).
 * @param buffer: The buffer to write the formatted string into.
 * @param len: The length of the buffer.
 * @param format: The format string.
 * @param ...: Additional arguments to format.
 * @returns: The length of the formatted string actually required (not including the null terminator).
 */
HO_PUBLIC_API size_t FormatString(char *buffer, size_t len, const char *format, ...);