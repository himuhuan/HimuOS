/**
 * HimuOperatingSystem
 *
 * File: log.h
 * Description:
 * Kernel log API with uptime timestamp prefix.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

HO_KERNEL_API uint64_t KLogWriteFmt(const char *fmt, ...);

#define klogf(fmt, ...) KLogWriteFmt(fmt, ##__VA_ARGS__)
