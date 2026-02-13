/**
 * HimuOperatingSystem
 *
 * File: ke/time/lapic_timer_sink.c
 * Description:
 * Ke Layer - Local APIC timer clockevent sink implementation
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "lapic_timer_sink.h"

#include <arch/amd64/asm.h>
#include <drivers/time/lapic_timer_driver.h>
#include <kernel/ke/mm.h>
#include <libc/string.h>

#define LAPIC_SPURIOUS_VECTOR 0xFF

static uint64_t Div128By64(uint64_t hi, uint64_t lo, uint64_t div);
static uint64_t Mul64Div64(uint64_t value, uint64_t mul, uint64_t div);
static HO_STATUS CalibrateLapicTimerForCpu(KE_LAPIC_TIMER_SINK *sink, uint32_t cpuIndex, KE_TIME_SINK *refSink);
static HO_STATUS LapicTimerSinkInitPerCpu(void *self, uint32_t cpuIndex, uint8_t vectorNumber);
static HO_STATUS LapicTimerSinkSetNextEvent(void *self, uint32_t cpuIndex, uint64_t deltaNs);
static void LapicTimerSinkAckInterrupt(void *self, uint32_t cpuIndex);
static uint64_t LapicTimerSinkGetFrequencyHz(void *self, uint32_t cpuIndex);
static uint32_t LapicTimerSinkGetCapabilityFlags(void *self);
static const char *LapicTimerSinkGetName(void *self);

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
    if (div == 0)
    {
        return 0;
    }

    __uint128_t product = (__uint128_t)value * mul;
    uint64_t hi = (uint64_t)(product >> 64);
    uint64_t lo = (uint64_t)product;
    return Div128By64(hi, lo, div);
}

static HO_STATUS
CalibrateLapicTimerForCpu(KE_LAPIC_TIMER_SINK *sink, uint32_t cpuIndex, KE_TIME_SINK *refSink)
{
    if (sink == NULL || refSink == NULL || refSink->ReadCounter == NULL || refSink->GetFrequency == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    uint64_t refFreqHz = refSink->GetFrequency(refSink);
    if (refFreqHz == 0)
    {
        return EC_INVALID_STATE;
    }

    const uint64_t kCalibrationWindowUs = 10000; // 10ms
    uint64_t refTicksWindow = Mul64Div64(kCalibrationWindowUs, refFreqHz, 1000000ULL);
    if (refTicksWindow == 0)
    {
        refTicksWindow = 1;
    }

    uint64_t samples[3];
    memset(samples, 0, sizeof(samples));

    for (uint32_t i = 0; i < 3; i++)
    {
        LapicTimerSetDivide(sink->ApicBaseVirt, sink->DividerCode);
        LapicTimerConfigure(sink->ApicBaseVirt, 0x20, LAPIC_TIMER_MODE_ONE_SHOT, TRUE);
        LapicTimerSetInitialCount(sink->ApicBaseVirt, 0xFFFFFFFFU);

        uint64_t refStart = refSink->ReadCounter(refSink);
        uint32_t lapicStart = LapicTimerReadCurrentCount(sink->ApicBaseVirt);

        while ((refSink->ReadCounter(refSink) - refStart) < refTicksWindow)
        {
            __asm__ __volatile__("pause");
        }

        uint32_t lapicEnd = LapicTimerReadCurrentCount(sink->ApicBaseVirt);
        if (lapicEnd > lapicStart)
        {
            return EC_FAILURE;
        }

        uint32_t elapsedTicks = lapicStart - lapicEnd;
        if (elapsedTicks == 0)
        {
            return EC_FAILURE;
        }

        samples[i] = Mul64Div64((uint64_t)elapsedTicks, refFreqHz, refTicksWindow);
        if (samples[i] == 0)
        {
            return EC_FAILURE;
        }
    }

    if (samples[0] > samples[1])
    {
        uint64_t temp = samples[0];
        samples[0] = samples[1];
        samples[1] = temp;
    }
    if (samples[1] > samples[2])
    {
        uint64_t temp = samples[1];
        samples[1] = samples[2];
        samples[2] = temp;
    }
    if (samples[0] > samples[1])
    {
        uint64_t temp = samples[0];
        samples[0] = samples[1];
        samples[1] = temp;
    }

    sink->FrequencyHzByCpu[cpuIndex] = samples[1];

    LapicTimerConfigure(sink->ApicBaseVirt, 0x20, LAPIC_TIMER_MODE_ONE_SHOT, TRUE);
    LapicTimerSetInitialCount(sink->ApicBaseVirt, 0);
    return EC_SUCCESS;
}

static HO_STATUS
LapicTimerSinkInitPerCpu(void *self, uint32_t cpuIndex, uint8_t vectorNumber)
{
    KE_LAPIC_TIMER_SINK *sink = (KE_LAPIC_TIMER_SINK *)self;

    if (sink == NULL || !sink->Initialized || cpuIndex >= KE_CLOCKEVENT_MAX_CPU)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    if (sink->FrequencyHzByCpu[cpuIndex] == 0)
    {
        return EC_INVALID_STATE;
    }

    sink->VectorByCpu[cpuIndex] = vectorNumber;
    LapicTimerSetDivide(sink->ApicBaseVirt, sink->DividerCode);
    LapicTimerConfigure(sink->ApicBaseVirt, vectorNumber, LAPIC_TIMER_MODE_ONE_SHOT, FALSE);
    LapicTimerSetInitialCount(sink->ApicBaseVirt, 0);
    sink->PerCpuInitialized[cpuIndex] = TRUE;
    return EC_SUCCESS;
}

static HO_STATUS
LapicTimerSinkSetNextEvent(void *self, uint32_t cpuIndex, uint64_t deltaNs)
{
    KE_LAPIC_TIMER_SINK *sink = (KE_LAPIC_TIMER_SINK *)self;

    if (sink == NULL || cpuIndex >= KE_CLOCKEVENT_MAX_CPU || !sink->PerCpuInitialized[cpuIndex])
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    uint64_t freqHz = sink->FrequencyHzByCpu[cpuIndex];
    if (freqHz == 0)
    {
        return EC_INVALID_STATE;
    }

    if (deltaNs == 0)
    {
        deltaNs = 1;
    }

    uint64_t ticks = Mul64Div64(deltaNs, freqHz, 1000000000ULL);
    if (ticks == 0)
    {
        ticks = 1;
    }

    if (ticks > 0xFFFFFFFFULL)
    {
        ticks = 0xFFFFFFFFULL;
    }

    LapicTimerConfigure(sink->ApicBaseVirt, sink->VectorByCpu[cpuIndex], LAPIC_TIMER_MODE_ONE_SHOT, FALSE);
    LapicTimerSetInitialCount(sink->ApicBaseVirt, (uint32_t)ticks);
    return EC_SUCCESS;
}

static void
LapicTimerSinkAckInterrupt(void *self, uint32_t cpuIndex)
{
    KE_LAPIC_TIMER_SINK *sink = (KE_LAPIC_TIMER_SINK *)self;
    (void)cpuIndex;

    if (sink == NULL || !sink->Initialized)
    {
        return;
    }

    LapicSendEoi(sink->ApicBaseVirt);
}

static uint64_t
LapicTimerSinkGetFrequencyHz(void *self, uint32_t cpuIndex)
{
    KE_LAPIC_TIMER_SINK *sink = (KE_LAPIC_TIMER_SINK *)self;
    if (sink == NULL || cpuIndex >= KE_CLOCKEVENT_MAX_CPU)
    {
        return 0;
    }

    return sink->FrequencyHzByCpu[cpuIndex];
}

static uint32_t
LapicTimerSinkGetCapabilityFlags(void *self)
{
    (void)self;
    return KE_CLOCKEVENT_CAP_ONE_SHOT_COUNTDOWN;
}

static const char *
LapicTimerSinkGetName(void *self)
{
    (void)self;
    return "LAPIC_TIMER";
}

HO_STATUS
KeLapicTimerSinkInit(KE_LAPIC_TIMER_SINK *sink, HO_PHYSICAL_ADDRESS acpiRsdpPhys, KE_TIME_SINK *refSink)
{
    if (sink == NULL || refSink == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    memset(sink, 0, sizeof(*sink));

    if (!LapicTimerIsSupported())
    {
        return EC_NOT_SUPPORTED;
    }

    HO_PHYSICAL_ADDRESS preferredBasePhys;
    memset(&preferredBasePhys, 0, sizeof(preferredBasePhys));
    if (LapicTimerDetectBaseFromAcpi(acpiRsdpPhys, &preferredBasePhys) != EC_SUCCESS)
    {
        preferredBasePhys = 0;
    }

    HO_STATUS status = LapicTimerEnableAndGetBase(preferredBasePhys, &sink->ApicBasePhys);
    if (status != EC_SUCCESS)
    {
        return status;
    }

    sink->ApicBaseVirt = HHDM_PHYS2VIRT(sink->ApicBasePhys);
    sink->DividerCode = LAPIC_TIMER_DIVIDE_BY_16;

    LapicMaskLegacyPic();
    LapicSetSpuriousVector(sink->ApicBaseVirt, LAPIC_SPURIOUS_VECTOR);

    status = CalibrateLapicTimerForCpu(sink, 0, refSink);
    if (status != EC_SUCCESS)
    {
        return status;
    }

    sink->Base.InitPerCpu = LapicTimerSinkInitPerCpu;
    sink->Base.SetNextEvent = LapicTimerSinkSetNextEvent;
    sink->Base.AckInterrupt = LapicTimerSinkAckInterrupt;
    sink->Base.GetFrequencyHz = LapicTimerSinkGetFrequencyHz;
    sink->Base.GetCapabilityFlags = LapicTimerSinkGetCapabilityFlags;
    sink->Base.GetName = LapicTimerSinkGetName;
    sink->Initialized = TRUE;
    return EC_SUCCESS;
}
