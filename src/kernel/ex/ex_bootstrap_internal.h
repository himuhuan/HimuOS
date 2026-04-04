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
#include <kernel/ke/mm.h>

struct KTHREAD;
struct KE_USER_BOOTSTRAP_STAGING;

typedef enum EX_OBJECT_TYPE
{
    EX_OBJECT_TYPE_INVALID = 0,
    EX_OBJECT_TYPE_PROCESS = 1,
    EX_OBJECT_TYPE_THREAD = 2,
} EX_OBJECT_TYPE;

typedef struct EX_OBJECT_HEADER
{
    EX_OBJECT_TYPE Type;
    uint32_t ReferenceCount;
} EX_OBJECT_HEADER;

struct EX_PROCESS
{
    EX_OBJECT_HEADER Header;
    KE_PROCESS_ADDRESS_SPACE AddressSpace;
    struct KE_USER_BOOTSTRAP_STAGING *Staging;
};

struct EX_THREAD
{
    EX_OBJECT_HEADER Header;
    struct KTHREAD *Thread;
    EX_PROCESS *Process;
};

void ExBootstrapInitializeProcessObject(EX_PROCESS *process);
void ExBootstrapInitializeThreadObject(EX_THREAD *thread);
HO_STATUS ExBootstrapTeardownProcessPayload(EX_PROCESS *process);
EX_PROCESS *ExBootstrapRetainProcess(EX_PROCESS *process);
HO_STATUS ExBootstrapReleaseProcess(EX_PROCESS *process);
HO_STATUS ExBootstrapReleaseThread(EX_THREAD *thread);

extern EX_PROCESS *gExBootstrapProcess;
extern EX_THREAD *gExBootstrapThread;