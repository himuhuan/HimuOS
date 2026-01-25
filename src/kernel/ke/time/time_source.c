/**
 * HimuOperatingSystem
 *
 * File: ke/time/time_source.c
 * Description:
 * Ke Layer - Time source device implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/time_source.h>
#include <arch/amd64/asm.h>
#include <arch/arch.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

#include "sinks/hpet_sink.h"
#include "sinks/tsc_sink.h"

//
// Device Singleton
//
static KE_TIME_DEVICE gTimeDevice;

//
// Sinks Storage
//
static KE_HPET_TIME_SINK gHpetSink;
static KE_TSC_TIME_SINK gTscSink;

//
// Math Helpers
//

// 128-bit by 64-bit division using x86-64 div instruction
// Computes (hi:lo) / div, returns quotient
static uint64_t
Div128By64(uint64_t hi, uint64_t lo, uint64_t div)
{
    uint64_t quot;
    __asm__ __volatile__("divq %3" : "=a"(quot) : "d"(hi), "a"(lo), "r"(div));
    return quot;
}

// Multiply then divide: (value * mul) / div
// Uses 128-bit intermediate to avoid overflow
static uint64_t
Mul64Div64(uint64_t value, uint64_t mul, uint64_t div)
{
    __uint128_t product = (__uint128_t)value * mul;
    uint64_t hi = (uint64_t)(product >> 64);
    uint64_t lo = (uint64_t)product;
    return Div128By64(hi, lo, div);
}

//
// Device Implementation
//

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeTimeSourceInit(HO_PHYSICAL_ADDRESS acpiRsdpPhys)
{
    memset(&gTimeDevice, 0, sizeof(gTimeDevice));
    
    // 1. Initialize Sinks
    HO_STATUS tscStatus = KeTscTimeSinkInit(&gTscSink);
    HO_STATUS hpetStatus = KeHpetTimeSinkInit(&gHpetSink, acpiRsdpPhys);

    // 2. Calibration Strategy
    if (tscStatus == EC_SUCCESS && !gTscSink.Calibrated)
    {
        if (!hpetStatus)
        {
             HO_STATUS calStatus = KeTscTimeSinkCalibrate(&gTscSink, &gHpetSink.Base);
             if (calStatus != EC_SUCCESS)
                 kprintf("[TIME] TSC calibration failed, trying rollback...\n");
        }
        else
        {
            kprintf("[TIME] Error: TSC needs calibration but HPET unavailable\n");
        }
    }

    KE_TIME_SINK *selectedSink = NULL;
    TIME_SOURCE_KIND kind = TIME_SOURCE_NONE;

    if (gTscSink.Initialized && gTscSink.Calibrated)
    {
        selectedSink = &gTscSink.Base;
        kind = TIME_SOURCE_TSC;
    }
    else if (gHpetSink.Initialized)
    {
        selectedSink = &gHpetSink.Base;
        kind = TIME_SOURCE_HPET;
    }

    if (!selectedSink)
    {
        kprintf("[TIME] No usable time source found\n");
        return EC_UNSUPPORTED_MACHINE;
    }

    // 4. Setup Device
    gTimeDevice.ActiveSink = selectedSink;
    gTimeDevice.Kind = kind;
    gTimeDevice.FreqHz = selectedSink->GetFrequency(selectedSink);
    gTimeDevice.StartTick = selectedSink->ReadCounter(selectedSink);
    gTimeDevice.Initialized = TRUE;

    kprintf("[TIME] Source: %s @ %lu Hz\n", selectedSink->GetName(selectedSink), gTimeDevice.FreqHz);

    return EC_SUCCESS;
}

HO_KERNEL_API uint64_t
KeGetSystemUpRealTime(void)
{
    if (!gTimeDevice.Initialized || !gTimeDevice.ActiveSink)
        return 0;

    uint64_t currentTick = gTimeDevice.ActiveSink->ReadCounter(gTimeDevice.ActiveSink);
    uint64_t elapsed = currentTick - gTimeDevice.StartTick;

    return Mul64Div64(elapsed, 1000000ULL, gTimeDevice.FreqHz);
}

HO_KERNEL_API TIME_SOURCE_KIND
KeGetTimeSourceKind(void)
{
    return gTimeDevice.Kind;
}

HO_KERNEL_API void
KeBusyWaitUs(uint64_t microsec)
{
    if (!gTimeDevice.Initialized || microsec == 0)
    {
        return;
    }

    uint64_t ticksToWait = Mul64Div64(microsec, gTimeDevice.FreqHz, 1000000ULL);
    if (ticksToWait == 0)
        ticksToWait = 1;

    uint64_t startTick = gTimeDevice.ActiveSink->ReadCounter(gTimeDevice.ActiveSink);

    while ((gTimeDevice.ActiveSink->ReadCounter(gTimeDevice.ActiveSink) - startTick) < ticksToWait)
    {
        __asm__ __volatile__("pause");
    }
}