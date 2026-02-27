/**
 * HimuOperatingSystem
 *
 * File: ke/time/clock_event.c
 * Description:
 * Ke Layer - clockevent device implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/clock_event.h>
#include <arch/amd64/idt.h>
#include <kernel/hodbg.h>
#include <kernel/ke/time_source.h>
#include <libc/string.h>

#include "sinks/lapic_clockevent_sink.h"

#define LAPIC_TIMER_VECTOR        0x40U
#define LAPIC_SPURIOUS_VECTOR     0xFFU
#define LAPIC_CALIBRATION_US      10000ULL
#define LAPIC_SELFTEST_DELTA_NS   5000000ULL
#define LAPIC_SELFTEST_TIMEOUT_US 50000ULL

static KE_CLOCK_EVENT_DEVICE gClockEventDevice;
static KE_LAPIC_CLOCK_EVENT_SINK gLapicClockEventSink;

static void LapicTimerInterruptHandler(void *frame, void *context);
static void LapicSpuriousInterruptHandler(void *frame, void *context);
static HO_STATUS ClockEventSelfTest(void);
static HO_STATUS WaitForTickAdvance(uint64_t baseCount, uint64_t neededDelta, uint64_t timeoutUs);

static void
LapicTimerInterruptHandler(void *frame, void *context)
{
    (void)frame;
    (void)context;
    KeClockEventOnInterrupt();
}

static void
LapicSpuriousInterruptHandler(void *frame, void *context)
{
    (void)frame;
    (void)context;
}

static HO_STATUS
WaitForTickAdvance(uint64_t baseCount, uint64_t neededDelta, uint64_t timeoutUs)
{
    uint64_t startUs = KeGetSystemUpRealTime();
    uint64_t nowUs = startUs;

    while ((nowUs - startUs) < timeoutUs)
    {
        uint64_t currentCount = gClockEventDevice.PerCpu[0].InterruptCount;
        if ((currentCount - baseCount) >= neededDelta)
            return EC_SUCCESS;

        KeBusyWaitUs(200);
        nowUs = KeGetSystemUpRealTime();
    }

    return EC_FAILURE;
}

static HO_STATUS
ClockEventSelfTest(void)
{
    uint64_t startCount = gClockEventDevice.PerCpu[0].InterruptCount;

    __asm__ __volatile__("sti" ::: "memory");

    for (uint32_t i = 0; i < 2; ++i)
    {
        uint64_t beforeCount = gClockEventDevice.PerCpu[0].InterruptCount;
        HO_STATUS status = KeClockEventSetNextEvent(LAPIC_SELFTEST_DELTA_NS);
        if (status != EC_SUCCESS)
        {
            __asm__ __volatile__("cli" ::: "memory");
            return status;
        }

        status = WaitForTickAdvance(beforeCount, 1, LAPIC_SELFTEST_TIMEOUT_US);
        if (status != EC_SUCCESS)
        {
            __asm__ __volatile__("cli" ::: "memory");
            return status;
        }
    }

    __asm__ __volatile__("cli" ::: "memory");

    uint64_t totalFired = gClockEventDevice.PerCpu[0].InterruptCount - startCount;
    if (totalFired != 2)
        return EC_FAILURE;

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventInit(void)
{
    memset(&gClockEventDevice, 0, sizeof(gClockEventDevice));

    HO_STATUS status = KeLapicClockEventSinkInit(&gLapicClockEventSink, LAPIC_TIMER_VECTOR);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR, "[CLKEV] LAPIC init failed: %ke\n", status);
        return status;
    }

    status = IdtRegisterInterruptHandler(LAPIC_TIMER_VECTOR, LapicTimerInterruptHandler, NULL);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR, "[CLKEV] failed to register LAPIC timer vector=%u\n", LAPIC_TIMER_VECTOR);
        return status;
    }

    status = IdtRegisterInterruptHandler(LAPIC_SPURIOUS_VECTOR, LapicSpuriousInterruptHandler, NULL);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR, "[CLKEV] failed to register LAPIC spurious vector=%u\n", LAPIC_SPURIOUS_VECTOR);
        return status;
    }

    status = KeLapicClockEventSinkCalibrate(&gLapicClockEventSink, LAPIC_CALIBRATION_US);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR, "[CLKEV] LAPIC calibration failed\n");
        return status;
    }

    gClockEventDevice.ActiveSink = &gLapicClockEventSink.Base;
    gClockEventDevice.ActiveSinkContext = &gLapicClockEventSink;
    gClockEventDevice.Mode = KE_CLOCK_EVENT_MODE_ONE_SHOT;
    gClockEventDevice.Kind = KE_CLOCK_EVENT_LAPIC_TIMER;
    gClockEventDevice.FreqHz = gLapicClockEventSink.TicksPerSec;
    gClockEventDevice.VectorNumber = LAPIC_TIMER_VECTOR;

    status = KeClockEventPerCpuInit(0);
    if (status != EC_SUCCESS)
    {
        klog(KLOG_LEVEL_ERROR, "[CLKEV] per-cpu init failed\n");
        return status;
    }

    gClockEventDevice.Initialized = TRUE;

    status = ClockEventSelfTest();
    if (status != EC_SUCCESS)
    {
        gClockEventDevice.Initialized = FALSE;
        klog(KLOG_LEVEL_ERROR, "[CLKEV] LAPIC timer self-test failed (vector/eoi/delivery)\n");
        return status;
    }

    gClockEventDevice.PerCpu[0].InterruptCount = 0;

    klog(KLOG_LEVEL_INFO, "[CLKEV] %s one-shot ready @ %lu Hz (vector=%u)\n",
         gClockEventDevice.ActiveSink->GetName(gClockEventDevice.ActiveSinkContext), gClockEventDevice.FreqHz,
         gClockEventDevice.VectorNumber);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventPerCpuInit(uint32_t cpuIndex)
{
    if (cpuIndex >= KE_CLOCK_EVENT_MAX_CPU_COUNT)
        return EC_ILLEGAL_ARGUMENT;

    if (!gLapicClockEventSink.Initialized || gLapicClockEventSink.TicksPerSec == 0)
        return EC_INVALID_STATE;

    LapicTimerConfigureOneShot(gLapicClockEventSink.BaseVirt, gLapicClockEventSink.VectorNumber,
                               gLapicClockEventSink.DividerValue, TRUE);
    LapicTimerSetInitialCount(gLapicClockEventSink.BaseVirt, 0U);

    gClockEventDevice.PerCpu[cpuIndex].Initialized = TRUE;
    gClockEventDevice.PerCpu[cpuIndex].InterruptCount = 0;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventSetNextEvent(uint64_t deltaNs)
{
    if (!gClockEventDevice.Initialized || gClockEventDevice.ActiveSink == NULL)
        return EC_INVALID_STATE;

    if (!gClockEventDevice.PerCpu[0].Initialized)
        return EC_INVALID_STATE;

    if (deltaNs == 0)
        klog(KLOG_LEVEL_WARNING, "[CLKEV] delta_ns=0 adjusted to minimum\n");

    return gClockEventDevice.ActiveSink->SetNextEventNs(gClockEventDevice.ActiveSinkContext, deltaNs);
}

HO_KERNEL_API BOOL
KeClockEventIsReady(void)
{
    return gClockEventDevice.Initialized;
}

HO_KERNEL_API uint64_t
KeClockEventGetInterruptCount(void)
{
    return gClockEventDevice.PerCpu[0].InterruptCount;
}

HO_KERNEL_API void
KeClockEventOnInterrupt(void)
{
    if (gClockEventDevice.PerCpu[0].Initialized)
        gClockEventDevice.PerCpu[0].InterruptCount++;

    KeLapicClockEventSinkSendEoi(&gLapicClockEventSink);
}
