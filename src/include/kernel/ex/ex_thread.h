/**
 * HimuOperatingSystem
 *
 * File: ex/ex_thread.h
 * Description: Bootstrap-scoped Ex thread wrapper and create parameters.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

/* Opaque owning handle for a bootstrap thread before runtime takeover. */
typedef struct EX_THREAD EX_THREAD;

typedef void (*EX_BOOTSTRAP_THREAD_ENTRY)(void *arg);

typedef struct EX_BOOTSTRAP_THREAD_CREATE_PARAMS
{
	EX_BOOTSTRAP_THREAD_ENTRY EntryPoint;
	void *EntryArg;
} EX_BOOTSTRAP_THREAD_CREATE_PARAMS;