/**
 * HimuOperatingSystem
 *
 * File: ke/time/pmtimer_sink.c
 * Description:
 * Ke Layer - PM Timer time sink implementation
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "pmtimer_sink.h"
#include <libc/string.h>

static uint64_t
PmTimerSinkReadCounter(void *self)
{
    KE_PMTIMER_TIME_SINK *sink = (KE_PMTIMER_TIME_SINK *)self;
    uint32_t raw = PmTimerRead(sink->Port);
    if (sink->BitWidth == 24)
    {
        raw &= 0x00FFFFFFU;
    }

    if (raw < sink->LastRaw)
    {
        sink->High += (1ULL << sink->BitWidth);
    }
    sink->LastRaw = raw;

    return sink->High | raw;
}

static uint64_t
PmTimerSinkGetFrequency(void *self)
{
    KE_PMTIMER_TIME_SINK *sink = (KE_PMTIMER_TIME_SINK *)self;
    return sink->FreqHz;
}

static const char *
PmTimerSinkGetName(void *self)
{
    (void)self;
    return "PMTIMER";
}

HO_STATUS
KePmTimerTimeSinkInit(KE_PMTIMER_TIME_SINK *sink, HO_PHYSICAL_ADDRESS acpiRsdpPhys)
{
    if (!sink)
        return EC_ILLEGAL_ARGUMENT;

    memset(sink, 0, sizeof(*sink));

    uint16_t port = 0;
    uint8_t bitWidth = 0;
    HO_STATUS status = PmTimerSetup(acpiRsdpPhys, &port, &bitWidth);
    if (status != EC_SUCCESS)
        return status;

    sink->Port = port;
    sink->BitWidth = bitWidth;
    sink->FreqHz = PM_TIMER_FREQ_HZ;
    sink->High = 0;
    sink->LastRaw = PmTimerRead(port) & ((bitWidth == 24) ? 0x00FFFFFFU : 0xFFFFFFFFU);

    sink->Base.ReadCounter = PmTimerSinkReadCounter;
    sink->Base.GetFrequency = PmTimerSinkGetFrequency;
    sink->Base.GetName = PmTimerSinkGetName;
    sink->Initialized = TRUE;

    return EC_SUCCESS;
}
