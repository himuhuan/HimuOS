/**
 * HimuOperatingSystem
 *
 * File: ke/mutex.h
 * Description:
 * Ke Layer - Kernel mutex object (KMUTEX).
 * Owner-aware dispatcher object backed by the unified wait model.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ke/dispatcher.h>

struct KTHREAD;

// ─────────────────────────────────────────────────────────────
// KMUTEX structure
// ─────────────────────────────────────────────────────────────

typedef struct KMUTEX
{
    KDISPATCHER_HEADER Header;
    struct KTHREAD *OwnerThread;
} KMUTEX;

// ─────────────────────────────────────────────────────────────
// KMUTEX API
// ─────────────────────────────────────────────────────────────

/**
 * @brief Initialize a kernel mutex object.
 * @param mutex Pointer to KMUTEX to initialize.
 */
HO_KERNEL_API void KeInitializeMutex(KMUTEX *mutex);

/**
 * @brief Release a kernel mutex owned by the current thread.
 * @param mutex Pointer to the KMUTEX.
 * @return EC_SUCCESS on success; EC_ILLEGAL_ARGUMENT on invalid arguments;
 *         EC_INVALID_STATE if the current thread does not own the mutex.
 */
HO_KERNEL_API HO_STATUS KeReleaseMutex(KMUTEX *mutex);
