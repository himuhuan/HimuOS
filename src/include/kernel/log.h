/**
 * HimuOperatingSystem PUBLIC HEADER
 *
 * File: log.h
 * Description: Kernel log APIs with optional uptime timestamp prefix.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

enum KE_LOG_LEVEL {
    KLOG_LEVEL_DEBUG = 0,
    KLOG_LEVEL_INFO,
    KLOG_LEVEL_WARNING,
    KLOG_LEVEL_ERROR,
};

HO_KERNEL_API uint64_t KLogWriteFmt(enum KE_LOG_LEVEL level, const char *fmt, ...);
