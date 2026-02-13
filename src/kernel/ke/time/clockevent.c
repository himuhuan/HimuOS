/**
 * HimuOperatingSystem
 *
 * File: ke/time/clockevent.c
 * Description:
 * Ke Layer - Clockevent device implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/clockevent.h>

#include <arch/amd64/asm.h>
#include <arch/amd64/idt.h>
#include <kernel/hodbg.h>
#include <kernel/ke/time_source.h>
#include <libc/string.h>

#include "sinks/lapic_timer_sink.h"

#define CLOCKEVENT_LAPIC_TIMER_VECTOR 0x40
#define CLOCKEVENT_PROBE_DELTA_NS     2000000ULL  // 2 ms
#define CLOCKEVENT_PROBE_TIMEOUT_US   20000ULL    // 20 ms

static KE_CLOCKEVENT_DEVICE gClockEventDevice;
static KE_LAPIC_TIMER_SINK gLapicTimerSink;

static void ClockEventInterruptHandler(uint8_t vectorNumber, KRNL_INTERRUPT_FRAME *frame, void *context);
static HO_STATUS ProbeCurrentCpuInterruptDelivery(void);

static void
ClockEventInterruptHandler(uint8_t vectorNumber, KRNL_INTERRUPT_FRAME *frame, void *context)
{
    (void)vectorNumber;
    (void)frame;

    KE_CLOCKEVENT_DEVICE *device = (KE_CLOCKEVENT_DEVICE *)context;
    if (device == NULL || device->ActiveSink == NULL)
    {
        return;
    }

    if (device->ActiveSink->AckInterrupt)
    {
        device->ActiveSink->AckInterrupt(device->ActiveSink, 0);
    }

    device->InterruptCount[0]++;

    if (device->Callback)
    {
        device->Callback(device->CallbackContext);
    }
}

static HO_STATUS
ProbeCurrentCpuInterruptDelivery(void)
{
    gClockEventDevice.InterruptCount[0] = 0;

    HO_STATUS status = gClockEventDevice.ActiveSink->SetNextEvent(gClockEventDevice.ActiveSink, 0, CLOCKEVENT_PROBE_DELTA_NS);
    if (status != EC_SUCCESS)
    {
        return status;
    }

    uint64_t startUs = KeGetSystemUpRealTime();
    while ((KeGetSystemUpRealTime() - startUs) < CLOCKEVENT_PROBE_TIMEOUT_US)
    {
        uint64_t interruptCount = gClockEventDevice.InterruptCount[0];
        if (interruptCount == 1)
        {
            return EC_SUCCESS;
        }

        if (interruptCount > 1)
        {
            return EC_INVALID_STATE;
        }

        __asm__ __volatile__("pause");
    }

    return EC_FAILURE;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventInit(HO_PHYSICAL_ADDRESS acpiRsdpPhys)
{
    memset(&gClockEventDevice, 0, sizeof(gClockEventDevice));

    if (!KeIsTimeSourceReady())
    {
        return EC_INVALID_STATE;
    }

    KE_TIME_SINK *refSink = KeTimeSourceGetActiveSink();
    if (refSink == NULL)
    {
        return EC_INVALID_STATE;
    }

    HO_STATUS status = KeLapicTimerSinkInit(&gLapicTimerSink, acpiRsdpPhys, refSink);
    if (status != EC_SUCCESS)
    {
        return status;
    }

    gClockEventDevice.ActiveSink = &gLapicTimerSink.Base;
    gClockEventDevice.TimerVector = CLOCKEVENT_LAPIC_TIMER_VECTOR;
    gClockEventDevice.CpuCount = 1;

    status = IdtRegisterInterruptHandler(gClockEventDevice.TimerVector, ClockEventInterruptHandler, &gClockEventDevice);
    if (status != EC_SUCCESS)
    {
        return status;
    }

    status = KeClockEventInitCurrentCpu();
    if (status != EC_SUCCESS)
    {
        (void)IdtUnregisterInterruptHandler(gClockEventDevice.TimerVector);
        return status;
    }

    BOOL irqEnabled = interrupt_is_enabled();
    if (!irqEnabled)
    {
        interrupt_enable();
    }

    status = ProbeCurrentCpuInterruptDelivery();

    if (!irqEnabled)
    {
        interrupt_disable();
    }

    if (status != EC_SUCCESS)
    {
        (void)IdtUnregisterInterruptHandler(gClockEventDevice.TimerVector);
        return status;
    }

    gClockEventDevice.Initialized = TRUE;

    uint64_t freqHz = gClockEventDevice.ActiveSink->GetFrequencyHz(gClockEventDevice.ActiveSink, 0);
    const char *name = gClockEventDevice.ActiveSink->GetName(gClockEventDevice.ActiveSink);
    kprintf("[CLK] Clockevent: %s @ %lu Hz (vector=0x%x)\n", name, freqHz, (uint64_t)gClockEventDevice.TimerVector);

    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventInitCurrentCpu(void)
{
    if (gClockEventDevice.ActiveSink == NULL || gClockEventDevice.ActiveSink->InitPerCpu == NULL)
    {
        return EC_INVALID_STATE;
    }

    return gClockEventDevice.ActiveSink->InitPerCpu(gClockEventDevice.ActiveSink, 0, gClockEventDevice.TimerVector);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventSetNextEvent(uint64_t deltaNs)
{
    if (!gClockEventDevice.Initialized || gClockEventDevice.ActiveSink == NULL ||
        gClockEventDevice.ActiveSink->SetNextEvent == NULL)
    {
        return EC_INVALID_STATE;
    }

    return gClockEventDevice.ActiveSink->SetNextEvent(gClockEventDevice.ActiveSink, 0, deltaNs);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventRegisterCallback(KE_CLOCKEVENT_CALLBACK callback, void *context)
{
    if (gClockEventDevice.ActiveSink == NULL)
    {
        return EC_INVALID_STATE;
    }

    gClockEventDevice.Callback = callback;
    gClockEventDevice.CallbackContext = context;
    return EC_SUCCESS;
}

HO_KERNEL_API BOOL
KeIsClockEventReady(void)
{
    return gClockEventDevice.Initialized;
}

HO_KERNEL_API uint64_t
KeClockEventGetFrequencyHz(void)
{
    if (!gClockEventDevice.Initialized || gClockEventDevice.ActiveSink == NULL ||
        gClockEventDevice.ActiveSink->GetFrequencyHz == NULL)
    {
        return 0;
    }

    return gClockEventDevice.ActiveSink->GetFrequencyHz(gClockEventDevice.ActiveSink, 0);
}
