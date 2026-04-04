/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_internal.h
 * Description: Private Ex bootstrap wrapper state shared inside ex/.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ex/ex_process.h>
#include <kernel/ex/ex_thread.h>

struct KTHREAD;
struct KE_USER_BOOTSTRAP_STAGING;

struct EX_PROCESS
{
    struct KE_USER_BOOTSTRAP_STAGING *Staging;
};

struct EX_THREAD
{
    struct KTHREAD *Thread;
    EX_PROCESS *Process;
};

extern EX_PROCESS *gExBootstrapProcess;
extern EX_THREAD *gExBootstrapThread;