/**
 * HimuOperatingSystem
 *
 * File: ex/ex_process.h
 * Description: Bootstrap-scoped Ex process wrapper and create parameters.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

/* Opaque owning handle for bootstrap process staging until transferred. */
typedef struct EX_PROCESS EX_PROCESS;

#define EX_MAX_PROCESSES           8u
#define EX_MAX_THREADS_PER_PROCESS 1u

typedef struct EX_BOOTSTRAP_PROCESS_CREATE_PARAMS
{
    const void *CodeBytes;
    uint64_t CodeLength;
    uint64_t EntryOffset;
    const void *ConstBytes;
    uint64_t ConstLength;
    uint32_t ProgramId;
    uint32_t ParentProcessId;
} EX_BOOTSTRAP_PROCESS_CREATE_PARAMS;
