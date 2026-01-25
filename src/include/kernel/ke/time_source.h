/**
 * HimuOperatingSystem
 *
 * File: ke/time_source.h
 * Description:
 * Ke Layer - Time source device interface
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include <kernel/ke/sinks/time_sink.h>

typedef enum TIME_SOURCE_KIND
{
    TIME_SOURCE_NONE = 0,
    TIME_SOURCE_TSC,
    TIME_SOURCE_HPET
} TIME_SOURCE_KIND;

/**
 * @brief Time Source Device (Singleton State)
 * Strictly follows Ke Device pattern: maintains state, manages sink.
 */
typedef struct KE_TIME_DEVICE
{
    KE_TIME_SINK *ActiveSink;
    uint64_t FreqHz;
    uint64_t StartTick;
    BOOL Initialized;
    TIME_SOURCE_KIND Kind;
} KE_TIME_DEVICE;

/**
 * @brief Initialize the global time source device.
 * @param acpiRsdpPhys Address of ACPI RSDP table.
 * @return Status code.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS
KeTimeSourceInit(HO_PHYSICAL_ADDRESS acpiRsdpPhys);

/**
 * @brief Get system uptime in microseconds.
 * @return Uptime in us.
 */
HO_KERNEL_API uint64_t KeGetSystemUpRealTime(void);

/**
 * @brief Get the type of the active time source.
 * @return Kind enum.
 */
HO_KERNEL_API TIME_SOURCE_KIND KeGetTimeSourceKind(void);

/**
 * @brief Busy wait for a specified number of microseconds.
 * @param us Microseconds to wait.
 */
HO_KERNEL_API void KeBusyWaitUs(uint64_t us);