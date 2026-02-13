/**
 * HimuOperatingSystem
 *
 * File: ke/clockevent.h
 * Description:
 * Ke Layer - Clockevent device interface
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ke/sinks/clockevent_sink.h>

typedef void (*KE_CLOCKEVENT_CALLBACK)(void *context);

typedef struct KE_CLOCKEVENT_DEVICE
{
    KE_CLOCKEVENT_SINK *ActiveSink;
    uint8_t TimerVector;
    uint32_t CpuCount;
    uint64_t InterruptCount[KE_CLOCKEVENT_MAX_CPU];
    KE_CLOCKEVENT_CALLBACK Callback;
    void *CallbackContext;
    BOOL Initialized;
} KE_CLOCKEVENT_DEVICE;

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventInit(HO_PHYSICAL_ADDRESS acpiRsdpPhys);

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventInitCurrentCpu(void);

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventSetNextEvent(uint64_t deltaNs);

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeClockEventRegisterCallback(KE_CLOCKEVENT_CALLBACK callback, void *context);

HO_KERNEL_API BOOL
KeIsClockEventReady(void);

HO_KERNEL_API uint64_t
KeClockEventGetFrequencyHz(void);
