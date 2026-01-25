/**
 * HimuOperatingSystem
 *
 * File: ke/time/tsc_sink.h
 * Description:
 * Ke Layer - TSC time sink
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ke/sinks/time_sink.h>
#include <drivers/time/tsc_driver.h>

typedef struct KE_TSC_TIME_SINK
{
    KE_TIME_SINK Base;
    uint64_t FreqHz;
    BOOL IsInvariant;
    BOOL Initialized;
    BOOL Calibrated;
} KE_TSC_TIME_SINK;

/**
 * @brief Initialize the TSC sink.
 * Detects TSC availability and attempts to determine frequency via CPUID.
 * @param sink Pointer to sink storage.
 * @return EC_SUCCESS if TSC is available (frequency might still be 0 if calibration needed).
 *         EC_NOT_SUPPORTED if TSC is missing or unusable.
 */
HO_STATUS KeTscTimeSinkInit(KE_TSC_TIME_SINK *sink);

/**
 * @brief Calibrate TSC using a reference time sink (e.g., HPET).
 * @param sink The TSC sink to calibrate.
 * @param refSink The reference sink (must be initialized and reliable).
 * @return EC_SUCCESS on successful calibration.
 */
HO_STATUS KeTscTimeSinkCalibrate(KE_TSC_TIME_SINK *sink, KE_TIME_SINK *refSink);
