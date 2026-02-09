/**
 * HimuOperatingSystem
 *
 * File: ke/log/log.c
 * Description: Ke layer log mechanism with uptime timestamp prefix.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/log.h>

#include <kernel/ke/console.h>
#include <kernel/ke/time_source.h>
#include <stdarg.h>

HO_KERNEL_API uint64_t
KLogWriteFmt(const char *fmt, ...)
{
    uint64_t written = 0;

    if (KeIsTimeSourceReady())
    {
        uint64_t uptimeUs = KeGetSystemUpRealTime();
        uint64_t sec = uptimeUs / 1000000ULL;
        uint64_t fracUs = uptimeUs % 1000000ULL;
        written += ConsoleWriteFmt("[%lu.%06lu] ", sec, fracUs);
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
