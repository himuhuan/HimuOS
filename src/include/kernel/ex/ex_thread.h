/**
 * HimuOperatingSystem
 *
 * File: ex/ex_thread.h
 * Description: Runtime-scoped Ex thread wrapper and create parameters.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

/* Opaque owning handle for a runtime thread before runtime takeover. */
typedef struct EX_THREAD EX_THREAD;

typedef void (*EX_RUNTIME_THREAD_ENTRY)(void *arg);

typedef uint32_t EX_RUNTIME_THREAD_CREATE_FLAGS;

#define EX_RUNTIME_THREAD_CREATE_FLAG_NONE     ((EX_RUNTIME_THREAD_CREATE_FLAGS)0u)
#define EX_RUNTIME_THREAD_CREATE_FLAG_JOINABLE ((EX_RUNTIME_THREAD_CREATE_FLAGS)0x00000001u)

typedef struct EX_RUNTIME_THREAD_CREATE_PARAMS
{
    EX_RUNTIME_THREAD_ENTRY EntryPoint;
    void *EntryArg;
    EX_RUNTIME_THREAD_CREATE_FLAGS Flags;
} EX_RUNTIME_THREAD_CREATE_PARAMS;
