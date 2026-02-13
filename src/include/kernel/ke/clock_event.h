/**
 * HimuOperatingSystem
 *
 * File: ke/clock_event.h
 * Description:
 * Ke Layer - clockevent device interface.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ke/sinks/clock_event_sink.h>

#define KE_CLOCK_EVENT_MAX_CPU_COUNT 1U

typedef enum KE_CLOCK_EVENT_MODE
{
    KE_CLOCK_EVENT_MODE_ONE_SHOT = 0,
    KE_CLOCK_EVENT_MODE_TSC_DEADLINE
} KE_CLOCK_EVENT_MODE;

typedef enum KE_CLOCK_EVENT_KIND
{
    KE_CLOCK_EVENT_NONE = 0,
    KE_CLOCK_EVENT_LAPIC_TIMER
} KE_CLOCK_EVENT_KIND;

typedef struct KE_CLOCK_EVENT_PERCPU_STATE
{
    BOOL Initialized;
    uint64_t InterruptCount;
} KE_CLOCK_EVENT_PERCPU_STATE;

typedef struct KE_CLOCK_EVENT_DEVICE
{
    KE_CLOCK_EVENT_SINK *ActiveSink;
    void *ActiveSinkContext;
    KE_CLOCK_EVENT_MODE Mode;
    KE_CLOCK_EVENT_KIND Kind;
    uint64_t FreqHz;
    uint8_t VectorNumber;
    BOOL Initialized;
    KE_CLOCK_EVENT_PERCPU_STATE PerCpu[KE_CLOCK_EVENT_MAX_CPU_COUNT];
} KE_CLOCK_EVENT_DEVICE;

HO_KERNEL_API HO_NODISCARD HO_STATUS KeClockEventInit(void);
HO_KERNEL_API HO_NODISCARD HO_STATUS KeClockEventPerCpuInit(uint32_t cpuIndex);
HO_KERNEL_API HO_NODISCARD HO_STATUS KeClockEventSetNextEvent(uint64_t deltaNs);
HO_KERNEL_API BOOL KeClockEventIsReady(void);
HO_KERNEL_API uint64_t KeClockEventGetInterruptCount(void);
HO_KERNEL_API void KeClockEventOnInterrupt(void);
