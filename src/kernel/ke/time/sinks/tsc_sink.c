/**
 * HimuOperatingSystem
 *
 * File: ke/time/tsc_sink.c
 * Description:
 * Ke Layer - TSC time sink implementation
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "tsc_sink.h"
#include <libc/string.h>
#include <arch/arch.h>
#include <kernel/hodbg.h>

static uint64_t
TscSinkReadCounter(void *self)
{
    (void)self;
    return TscReadTick();
}

static uint64_t
TscSinkGetFrequency(void *self)
{
    KE_TSC_TIME_SINK *sink = (KE_TSC_TIME_SINK *)self;
    return sink->FreqHz;
}

static const char *
TscSinkGetName(void *self)
{
    (void)self;
    return "TSC";
}

HO_STATUS
KeTscTimeSinkInit(KE_TSC_TIME_SINK *sink)
{
    if (!sink)
        return EC_ILLEGAL_ARGUMENT;

    memset(sink, 0, sizeof(*sink));

    ARCH_BASIC_CPU_INFO cpuInfo;
    memset(&cpuInfo, 0, sizeof(cpuInfo));
    GetBasicCpuInfo(&cpuInfo);

    if (!TscDetectAvailable(&cpuInfo))
    {
        return EC_NOT_SUPPORTED;
    }

    sink->IsInvariant = TscDetectInvariant(&cpuInfo);
    BOOL isHypervisor = cpuInfo.IsRunningInHypervisor;

    if (!sink->IsInvariant && !isHypervisor)
    {
        return EC_NOT_SUPPORTED;
    }

    sink->FreqHz = TscGetFreqFromCpuid(&cpuInfo);
    
    // Set up interface
    sink->Base.ReadCounter = TscSinkReadCounter;
    sink->Base.GetFrequency = TscSinkGetFrequency;
    sink->Base.GetName = TscSinkGetName;
    sink->Initialized = TRUE;
    sink->Calibrated = (sink->FreqHz != 0);

    return EC_SUCCESS;
}

HO_STATUS
KeTscTimeSinkCalibrate(KE_TSC_TIME_SINK *sink, KE_TIME_SINK *refSink)
{
    if (!sink || !refSink || !sink->Initialized)
        return EC_ILLEGAL_ARGUMENT;

    uint64_t refFreq = refSink->GetFrequency(refSink);
    if (refFreq == 0)
        return EC_INVALID_STATE;

    const int NUM_ITERATIONS = 5;
    const int DISCARD_EXTREMES = 2;

    uint64_t refTicksFor10ms = refFreq / 100;
    uint64_t results[5]; // constant size to avoid VLA

    for (int i = 0; i < NUM_ITERATIONS; i++)
    {
        uint64_t refStart = refSink->ReadCounter(refSink);
        uint64_t tscStart = TscReadTick();

        while ((refSink->ReadCounter(refSink) - refStart) < refTicksFor10ms)
        {
            __asm__ __volatile__("pause");
        }

        uint64_t tscEnd = TscReadTick();
        results[i] = (tscEnd - tscStart) * 100; // Extrapolate to 1 sec

        // Small delay between iterations
        if (i < NUM_ITERATIONS - 1)
        {
            uint64_t delayStart = refSink->ReadCounter(refSink);
            while ((refSink->ReadCounter(refSink) - delayStart) < refTicksFor10ms / 10)
            {
                __asm__ __volatile__("pause");
            }
        }
    }

    // Sort results
    for (int i = 0; i < NUM_ITERATIONS - 1; i++)
    {
        for (int j = i + 1; j < NUM_ITERATIONS; j++)
        {
            if (results[i] > results[j])
            {
                uint64_t temp = results[i];
                results[i] = results[j];
                results[j] = temp;
            }
        }
    }

    // Average middle results
    uint64_t sum = 0;
    for (int i = DISCARD_EXTREMES; i < NUM_ITERATIONS - DISCARD_EXTREMES; i++)
    {
        sum += results[i];
    }

    sink->FreqHz = sum / (NUM_ITERATIONS - 2 * DISCARD_EXTREMES);
    sink->Calibrated = TRUE;

    return EC_SUCCESS;
}
