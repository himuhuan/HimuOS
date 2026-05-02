/**
 * HimuOperatingSystem
 *
 * File: ex/runtime_internal.h
 * Description: Private Ex runtime wrapper state shared inside ex/.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ex/handle.h>
#include <kernel/ex/object.h>
#include <kernel/ex/ex_process.h>
#include <kernel/ex/ex_thread.h>
#include <kernel/ex/user_sysinfo_abi.h>
#include <kernel/ke/event.h>
#include <kernel/ke/mm.h>

struct KTHREAD;
struct KE_USER_MODE_STAGING;
struct EX_PROCESS;

typedef enum EX_PROCESS_STATE
{
    EX_PROCESS_STATE_CREATED = 0,
    EX_PROCESS_STATE_READY,
    EX_PROCESS_STATE_TERMINATED,
} EX_PROCESS_STATE;

typedef enum EX_PROCESS_TERMINATION_REASON
{
    EX_PROCESS_TERMINATION_REASON_NONE = 0,
    EX_PROCESS_TERMINATION_REASON_EXIT,
    EX_PROCESS_TERMINATION_REASON_KILL,
    EX_PROCESS_TERMINATION_REASON_FAULT,
} EX_PROCESS_TERMINATION_REASON;

typedef struct EX_STDOUT_SERVICE
{
    EX_OBJECT_HEADER Header;
    struct EX_PROCESS *Owner;
} EX_STDOUT_SERVICE;

struct EX_PROCESS
{
    EX_OBJECT_HEADER Header;
    KE_PROCESS_ADDRESS_SPACE AddressSpace;
    struct KE_USER_MODE_STAGING *Staging;
    EX_HANDLE SelfHandle;
    EX_HANDLE StdoutHandle;
    EX_HANDLE WaitHandle;
    uint32_t ProcessId;
    uint32_t ParentProcessId;
    uint32_t MainThreadId;
    EX_PROCESS_STATE State;
    uint64_t ExitStatus;
    EX_PROCESS_TERMINATION_REASON TerminationReason;
    BOOL KillRequested;
    BOOL CompletionRetained;
    BOOL CompletionSignaled;
    BOOL Foreground;
    uint32_t RestoreForegroundOwnerThreadId;
    uint32_t ProgramId;
    KEVENT CompletionEvent;
    EX_STDOUT_SERVICE StdoutService;
    EX_HANDLE_TABLE HandleTable;
};

struct EX_THREAD
{
    EX_OBJECT_HEADER Header;
    struct KTHREAD *Thread;
    EX_PROCESS *Process;
    EX_HANDLE SelfHandle;
    uint32_t ThreadId;
    BOOL CompletionSignaled;
    KEVENT CompletionEvent;
};

void ExRuntimeInitializeStdoutServiceObject(EX_PROCESS *process);
HO_STATUS ExRuntimeReleaseStdoutServiceOwner(EX_PROCESS *process);
void ExRuntimeInitializeProcessObject(EX_PROCESS *process);
void ExRuntimeInitializeThreadObject(EX_THREAD *thread);
HO_STATUS ExRuntimeTeardownProcessPayload(EX_PROCESS *process);
EX_PROCESS *ExRuntimeRetainProcess(EX_PROCESS *process);
HO_STATUS ExRuntimeReleaseProcess(EX_PROCESS *process);
HO_STATUS ExRuntimeReleaseThread(EX_THREAD *thread);
BOOL ExRuntimeIsPublishedObject(const EX_OBJECT_HEADER *objectHeader);
BOOL ExRuntimeIsProcessPublished(const EX_PROCESS *process);
HO_STATUS ExRuntimeCaptureThreadList(EX_SYSINFO_THREAD_LIST *outThreadList);
HO_STATUS ExRuntimeCaptureProcessList(EX_SYSINFO_PROCESS_LIST *outProcessList);
HO_STATUS ExRuntimePublishThread(EX_PROCESS *process, EX_THREAD *thread);
EX_THREAD *ExRuntimeLookupThreadByKernelThread(const struct KTHREAD *thread);
EX_PROCESS *ExRuntimeLookupProcessByKernelThread(const struct KTHREAD *thread);
EX_PROCESS *ExRuntimeLookupProcessByPid(uint32_t processId);
HO_STATUS ExRuntimeRetainChildProcess(uint32_t parentProcessId, uint32_t childProcessId, EX_PROCESS **outProcess);
HO_STATUS ExRuntimeQueryCurrentProcessId(uint32_t *outProcessId);
HO_STATUS ExRuntimeSetProcessForeground(EX_PROCESS *process, uint32_t restoreForegroundOwnerThreadId);
HO_STATUS ExRuntimeRequestProcessKill(EX_PROCESS *process);
BOOL ExRuntimeShouldTerminateCurrentProcess(uint32_t *outProgramId);
HO_STATUS ExRuntimeMarkProcessControl(EX_PROCESS *process, BOOL foreground, uint32_t restoreForegroundOwnerThreadId);
HO_STATUS ExRuntimeMarkCurrentProcessTerminating(EX_PROCESS_TERMINATION_REASON reason, uint64_t exitStatus);
HO_STATUS ExRuntimeMarkProcessTerminating(EX_PROCESS *process,
                                          EX_PROCESS_TERMINATION_REASON reason,
                                          uint64_t exitStatus);
HO_STATUS ExRuntimeMarkProcessTerminated(EX_PROCESS *process);
HO_STATUS ExRuntimeSignalProcessCompletion(EX_PROCESS *process);
HO_STATUS ExRuntimeSignalThreadCompletion(EX_THREAD *thread);
HO_STATUS ExRuntimeWaitForProcessCompletion(EX_PROCESS *process, uint64_t timeoutNs);
HO_STATUS ExRuntimeWaitForThreadCompletion(EX_THREAD *thread, uint64_t timeoutNs);
HO_STATUS ExRuntimeConsumeCompletedProcess(EX_PROCESS *process);
void ExRuntimeUnpublishThreadByKernelThread(const struct KTHREAD *thread,
                                            EX_THREAD **outThread,
                                            EX_PROCESS **outProcess);
void ExRuntimeUnpublishByKernelThread(const struct KTHREAD *thread, EX_THREAD **outThread, EX_PROCESS **outProcess);
HO_STATUS ExRuntimeBuildInitialConstBytes(const EX_RUNTIME_PROCESS_CREATE_PARAMS *params,
                                          uint8_t **outConstBytes,
                                          uint64_t *outConstLength);
HO_STATUS ExRuntimePatchCapabilitySeed(EX_PROCESS *process, EX_THREAD *thread);
int64_t ExRuntimeHandleQuerySysinfo(EX_PROCESS *process, uint64_t infoClassRaw, uint64_t userBuffer, uint64_t length);
