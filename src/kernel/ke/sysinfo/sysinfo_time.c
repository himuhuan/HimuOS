/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo_time.c
 * Description: Time source, uptime, clock event, and scheduler queries.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"
#include <kernel/ke/sysinfo.h>
#include <kernel/ke/time_source.h>
#include <kernel/ke/clock_event.h>
#include <kernel/ke/scheduler.h>
#include <kernel/hodefs.h>
#include <libc/string.h>

HO_STATUS
QueryTimeSource(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_TIME_SOURCE);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    if (!KeIsTimeSourceReady())
        return EC_INVALID_STATE;

    SYSINFO_TIME_SOURCE *info = (SYSINFO_TIME_SOURCE *)Buffer;
    memset(info, 0, sizeof(*info));

    TIME_SOURCE_KIND kind = KeGetTimeSourceKind();
    const char *name;

    switch (kind)
    {
    case TIME_SOURCE_TSC:
        name = "TSC";
        break;
    case TIME_SOURCE_PM_TIMER:
        name = "PM Timer";
        break;
    case TIME_SOURCE_HPET:
        name = "HPET";
        break;
    default:
        name = "Unknown";
        break;
    }

    size_t nameLen = strlen(name);
    if (nameLen >= SYSINFO_TIME_SOURCE_NAME_LEN)
        nameLen = SYSINFO_TIME_SOURCE_NAME_LEN - 1;
    memcpy(info->Name, name, nameLen);
    info->Name[nameLen] = '\0';

    info->Frequency = KeGetTimeSourceFrequency();
    info->Features = 0;

    return EC_SUCCESS;
}

HO_STATUS
QueryUptime(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_UPTIME);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    if (!KeIsTimeSourceReady())
        return EC_INVALID_STATE;

    SYSINFO_UPTIME *info = (SYSINFO_UPTIME *)Buffer;
    info->Nanoseconds = KeGetSystemUpRealTime() * 1000ULL;

    return EC_SUCCESS;
}

HO_STATUS
QueryClockEvent(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_CLOCK_EVENT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_CLOCK_EVENT *info = (SYSINFO_CLOCK_EVENT *)Buffer;
    memset(info, 0, sizeof(*info));

    info->Ready = KeClockEventIsReady();

    if (!info->Ready)
        return EC_SUCCESS;

    info->FreqHz = KeClockEventGetFrequency();
    info->InterruptCount = KeClockEventGetInterruptCount();
    info->VectorNumber = KeClockEventGetVector();

    const char *name = KeClockEventGetSourceName();
    if (name)
    {
        size_t len = strlen(name);
        if (len >= sizeof(info->SourceName))
            len = sizeof(info->SourceName) - 1;
        memcpy(info->SourceName, name, len);
        info->SourceName[len] = '\0';
    }

    return EC_SUCCESS;
}

HO_STATUS
QueryScheduler(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(KE_SYSINFO_SCHEDULER_DATA);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    return KeQuerySchedulerInfo((KE_SYSINFO_SCHEDULER_DATA *)Buffer);
}
