/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/time_sink.h
 * Description:
 * Ke Layer - Time sink interface
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

typedef struct KE_TIME_SINK
{
    /**
     * @brief Read current counter value from the sink.
     * @param self Pointer to the sink instance.
     * @return Current tick count.
     */
    uint64_t (*ReadCounter)(void *self);

    /**
     * @brief Get the frequency of the sink counter.
     * @param self Pointer to the sink instance.
     * @return Frequency in Hz.
     */
    uint64_t (*GetFrequency)(void *self);

    /**
     * @brief Get the name of the sink.
     * @param self Pointer to the sink instance.
     * @return String literal name.
     */
    const char *(*GetName)(void *self);

} KE_TIME_SINK;
