/**
 * HimuOperatingSystem
 *
 * File: ke/log/log.c
 * Description:
 * Kernel log implementation with uptime timestamp prefix.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/log.h>
#include <kernel/console.h>
#include <kernel/ke/time_source.h>
#include <stdarg.h>

#define USEC_PER_SEC 1000000ULL

HO_KERNEL_API uint64_t
KLogWriteFmt(const char *fmt, ...)
{
    uint64_t written = 0;

    if (KeIsTimeSourceReady())
    {
        uint64_t us = KeGetSystemUpRealTime();
        uint64_t sec = us / USEC_PER_SEC;
        uint64_t frac = us % USEC_PER_SEC;
        written += ConsoleWriteFmt("[%lu.%06lu] ", sec, frac);
    }
    else
    {
        written += ConsoleWrite("[----.------] ");
    }

    VA_LIST args;
    VA_START(args, fmt);
    written += ConsoleWriteVFmt(fmt, args);
    VA_END(args);

    return written;
}
