/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo/scheduler.c
 * Description:
 * Scheduler-oriented system information query handlers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sysinfo_internal.h"

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
