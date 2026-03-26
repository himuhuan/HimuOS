/**
 * HimuOperatingSystem
 *
 * File: ke/event.h
 * Description:
 * Ke Layer - Kernel event object (KEVENT) — first dispatcher object.
 * Manual-reset semantics: stays signaled until explicitly reset.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ke/dispatcher.h>

// ─────────────────────────────────────────────────────────────
// KEVENT structure
// ─────────────────────────────────────────────────────────────

typedef struct KEVENT
{
    KDISPATCHER_HEADER Header;
} KEVENT;

// ─────────────────────────────────────────────────────────────
// KEVENT API
// ─────────────────────────────────────────────────────────────

/**
 * @brief Initialize a kernel event object.
 * @param event       Pointer to KEVENT to initialize.
 * @param initialState TRUE = signaled, FALSE = non-signaled.
 */
HO_KERNEL_API void KeInitializeEvent(KEVENT *event, BOOL initialState);

/**
 * @brief Set an event to the signaled state.
 *        All threads currently waiting on this event are released.
 *        The event remains signaled until KeResetEvent is called.
 * @param event Pointer to the KEVENT.
 */
HO_KERNEL_API void KeSetEvent(KEVENT *event);

/**
 * @brief Reset an event to the non-signaled state.
 * @param event Pointer to the KEVENT.
 */
HO_KERNEL_API void KeResetEvent(KEVENT *event);
