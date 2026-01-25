/**
 * HimuOperatingSystem
 *
 * File: ke/time/hpet_sink.c
 * Description:
 * Ke Layer - HPET time sink implementation
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "hpet_sink.h"
#include <libc/string.h>

static uint64_t
HpetSinkReadCounter(void *self)
{
    KE_HPET_TIME_SINK *sink = (KE_HPET_TIME_SINK *)self;
    return HpetReadMainCounterAt(sink->BaseVirt);
}

static uint64_t
HpetSinkGetFrequency(void *self)
{
    KE_HPET_TIME_SINK *sink = (KE_HPET_TIME_SINK *)self;
    return sink->FreqHz;
}

static const char *
HpetSinkGetName(void *self)
{
    (void)self;
    return "HPET";
}

HO_STATUS
KeHpetTimeSinkInit(KE_HPET_TIME_SINK *sink, HO_PHYSICAL_ADDRESS acpiRsdpPhys)
{
    if (!sink)
        return EC_ILLEGAL_ARGUMENT;

    memset(sink, 0, sizeof(*sink));

    HO_STATUS status = HpetSetup(acpiRsdpPhys, &sink->BaseVirt, &sink->FreqHz);
    if (status != EC_SUCCESS)
    {
        return status;
    }

    sink->Base.ReadCounter = HpetSinkReadCounter;
    sink->Base.GetFrequency = HpetSinkGetFrequency;
    sink->Base.GetName = HpetSinkGetName;
    sink->Initialized = TRUE;

    return EC_SUCCESS;
}
