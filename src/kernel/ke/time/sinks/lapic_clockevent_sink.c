/**
 * HimuOperatingSystem
 *
 * File: ke/time/sinks/lapic_clockevent_sink.c
 * Description:
 * Ke Layer - Local APIC clockevent sink implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "lapic_clockevent_sink.h"
#include <kernel/ke/time_source.h>
#include <libc/string.h>

static uint64_t Div128By64(uint64_t hi, uint64_t lo, uint64_t div);
static uint64_t Mul64Div64(uint64_t value, uint64_t mul, uint64_t div);
static HO_STATUS LapicClockEventSetNextEventNs(void *self, uint64_t deltaNs);
static uint64_t LapicClockEventGetMinDeltaNs(void *self);
static uint64_t LapicClockEventGetMaxDeltaNs(void *self);
static const char *LapicClockEventGetName(void *self);

static uint64_t
Div128By64(uint64_t hi, uint64_t lo, uint64_t div)
{
    uint64_t quot;
    __asm__ __volatile__("divq %3" : "=a"(quot) : "d"(hi), "a"(lo), "r"(div));
    return quot;
}

static uint64_t
Mul64Div64(uint64_t value, uint64_t mul, uint64_t div)
{
    __uint128_t product = (__uint128_t)value * mul;
    uint64_t hi = (uint64_t)(product >> 64);
    uint64_t lo = (uint64_t)product;
    return Div128By64(hi, lo, div);
}

static HO_STATUS
LapicClockEventSetNextEventNs(void *self, uint64_t deltaNs)
{
    KE_LAPIC_CLOCK_EVENT_SINK *sink = (KE_LAPIC_CLOCK_EVENT_SINK *)self;

    if (sink == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!sink->Initialized || sink->TicksPerSec == 0)
        return EC_INVALID_STATE;

    if (deltaNs == 0)
        deltaNs = 1;

    uint64_t ticks = Mul64Div64(deltaNs, sink->TicksPerSec, 1000000000ULL);
    if (ticks == 0)
        ticks = 1;

    if (ticks > 0xFFFFFFFFULL)
        ticks = 0xFFFFFFFFULL;

    LapicTimerConfigureOneShot(sink->BaseVirt, sink->VectorNumber, sink->DividerValue, FALSE);
    LapicTimerSetInitialCount(sink->BaseVirt, (uint32_t)ticks);
    return EC_SUCCESS;
}

static uint64_t
LapicClockEventGetMinDeltaNs(void *self)
{
    KE_LAPIC_CLOCK_EVENT_SINK *sink = (KE_LAPIC_CLOCK_EVENT_SINK *)self;

    if (sink == NULL || sink->TicksPerSec == 0)
        return 0;

    return (1000000000ULL + sink->TicksPerSec - 1) / sink->TicksPerSec;
}

static uint64_t
LapicClockEventGetMaxDeltaNs(void *self)
{
    KE_LAPIC_CLOCK_EVENT_SINK *sink = (KE_LAPIC_CLOCK_EVENT_SINK *)self;

    if (sink == NULL || sink->TicksPerSec == 0)
        return 0;

    return Mul64Div64(0xFFFFFFFFULL, 1000000000ULL, sink->TicksPerSec);
}

static const char *
LapicClockEventGetName(void *self)
{
    (void)self;
    return "LAPIC";
}

HO_STATUS
KeLapicClockEventSinkInit(KE_LAPIC_CLOCK_EVENT_SINK *sink, uint8_t vectorNumber)
{
    if (sink == NULL)
        return EC_ILLEGAL_ARGUMENT;

    memset(sink, 0, sizeof(*sink));

    HO_STATUS status = LapicDetectAndEnable(&sink->BasePhys, &sink->BaseVirt);
    if (status != EC_SUCCESS)
        return status;

    sink->DividerValue = LAPIC_TIMER_DIVIDE_BY_16;
    sink->VectorNumber = vectorNumber;

    LapicSetSpuriousVector(sink->BaseVirt, 0xFFU);
    LapicTimerConfigureOneShot(sink->BaseVirt, sink->VectorNumber, sink->DividerValue, TRUE);
    LapicTimerSetInitialCount(sink->BaseVirt, 0U);

    sink->Base.SetNextEventNs = LapicClockEventSetNextEventNs;
    sink->Base.GetMinDeltaNs = LapicClockEventGetMinDeltaNs;
    sink->Base.GetMaxDeltaNs = LapicClockEventGetMaxDeltaNs;
    sink->Base.GetName = LapicClockEventGetName;
    sink->Initialized = TRUE;

    return EC_SUCCESS;
}

HO_STATUS
KeLapicClockEventSinkCalibrate(KE_LAPIC_CLOCK_EVENT_SINK *sink, uint64_t windowUs)
{
    if (sink == NULL || windowUs == 0)
        return EC_ILLEGAL_ARGUMENT;

    if (!sink->Initialized)
        return EC_INVALID_STATE;

    LapicTimerConfigureOneShot(sink->BaseVirt, sink->VectorNumber, sink->DividerValue, FALSE);
    LapicTimerSetInitialCount(sink->BaseVirt, 0xFFFFFFFFU);

    uint64_t startUs = KeGetSystemUpRealTime();
    uint32_t startCount = LapicTimerGetCurrentCount(sink->BaseVirt);
    uint64_t nowUs = startUs;

    while ((nowUs - startUs) < windowUs)
    {
        KeBusyWaitUs(200);
        nowUs = KeGetSystemUpRealTime();
    }

    uint32_t endCount = LapicTimerGetCurrentCount(sink->BaseVirt);

    LapicTimerConfigureOneShot(sink->BaseVirt, sink->VectorNumber, sink->DividerValue, TRUE);
    LapicTimerSetInitialCount(sink->BaseVirt, 0U);

    uint64_t elapsedUs = nowUs - startUs;
    if (elapsedUs == 0 || startCount <= endCount)
        return EC_FAILURE;

    uint64_t consumedTicks = (uint64_t)(startCount - endCount);
    uint64_t ticksPerSec = Mul64Div64(consumedTicks, 1000000ULL, elapsedUs);
    if (ticksPerSec == 0)
        return EC_FAILURE;

    sink->TicksPerSec = ticksPerSec;
    return EC_SUCCESS;
}

void
KeLapicClockEventSinkSendEoi(KE_LAPIC_CLOCK_EVENT_SINK *sink)
{
    if (sink == NULL || !sink->Initialized)
        return;

    LapicSendEoi(sink->BaseVirt);
}
