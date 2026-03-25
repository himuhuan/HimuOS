/**
 * HimuOperatingSystem
 *
 * File: ke/semaphore.h
 * Description:
 * Ke Layer - Kernel semaphore object (KSEMAPHORE).
 * Counted dispatcher object backed by the unified wait model.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ke/dispatcher.h>

// ─────────────────────────────────────────────────────────────
// KSEMAPHORE structure
// ─────────────────────────────────────────────────────────────

typedef struct KSEMAPHORE
{
    KDISPATCHER_HEADER Header;
    int32_t Limit;
} KSEMAPHORE;

// ─────────────────────────────────────────────────────────────
// KSEMAPHORE API
// ─────────────────────────────────────────────────────────────

/**
 * @brief Initialize a kernel semaphore object.
 * @param semaphore    Pointer to KSEMAPHORE to initialize.
 * @param initialCount Initial number of available permits.
 * @param limit        Maximum number of permits the semaphore can hold.
 * @return EC_SUCCESS on success; EC_ILLEGAL_ARGUMENT on invalid parameters.
 */
HO_KERNEL_API HO_STATUS KeInitializeSemaphore(KSEMAPHORE *semaphore, int32_t initialCount, int32_t limit);

/**
 * @brief Release one or more permits to a semaphore.
 * @param semaphore    Pointer to the KSEMAPHORE.
 * @param releaseCount Number of permits to release.
 * @return EC_SUCCESS on success; EC_ILLEGAL_ARGUMENT on invalid parameters or overflow.
 */
HO_KERNEL_API HO_STATUS KeReleaseSemaphore(KSEMAPHORE *semaphore, int32_t releaseCount);
