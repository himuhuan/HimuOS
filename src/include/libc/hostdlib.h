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

HO_PUBLIC_API char *Int32ToString(int32_t value, char *out, int base, BOOL prefix);

HO_PUBLIC_API char *Int64ToString(int64_t value, char *out, int base, BOOL prefix);

HO_PUBLIC_API char *UInt32ToString(uint32_t value, char *out, int base, BOOL prefix);

HO_PUBLIC_API char *UInt64ToString(uint64_t value, char *out, int base, BOOL prefix);