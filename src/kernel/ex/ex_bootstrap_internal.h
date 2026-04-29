/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap_internal.h
 * Description: Private Ex bootstrap wrapper state shared inside ex/.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ex/ex_bootstrap_abi.h>
#include <kernel/ex/handle.h>
#include <kernel/ex/object.h>
#include <kernel/ex/ex_process.h>
#include <kernel/ex/ex_thread.h>
#include <kernel/ke/mm.h>

struct KTHREAD;
struct KE_USER_BOOTSTRAP_STAGING;
struct EX_PROCESS;

typedef enum EX_PROCESS_STATE
{
    EX_PROCESS_STATE_CREATED = 0,
    EX_PROCESS_STATE_READY,
    EX_PROCESS_STATE_TERMINATED,
} EX_PROCESS_STATE;

typedef EX_HANDLE EX_PRIVATE_HANDLE;
typedef EX_HANDLE_RIGHTS EX_PRIVATE_HANDLE_RIGHTS;
typedef EX_HANDLE_SLOT EX_PRIVATE_HANDLE_SLOT;
typedef EX_HANDLE_TABLE EX_PRIVATE_HANDLE_TABLE;

#define EX_PRIVATE_HANDLE_INVALID            EX_HANDLE_INVALID
#define EX_PRIVATE_HANDLE_TABLE_CAPACITY     EX_MAX_HANDLES_PER_PROCESS

#define EX_PRIVATE_HANDLE_RIGHT_NONE         EX_HANDLE_RIGHT_NONE
#define EX_PRIVATE_HANDLE_RIGHT_QUERY        EX_HANDLE_RIGHT_QUERY
#define EX_PRIVATE_HANDLE_RIGHT_CLOSE        EX_HANDLE_RIGHT_CLOSE
#define EX_PRIVATE_HANDLE_RIGHT_PROCESS_SELF EX_HANDLE_RIGHT_PROCESS_SELF
#define EX_PRIVATE_HANDLE_RIGHT_THREAD_SELF  EX_HANDLE_RIGHT_THREAD_SELF
#define EX_PRIVATE_HANDLE_RIGHT_WRITE        EX_HANDLE_RIGHT_WRITE
#define EX_PRIVATE_HANDLE_RIGHT_WAIT         EX_HANDLE_RIGHT_WAIT

typedef struct EX_STDOUT_SERVICE
{
    EX_OBJECT_HEADER Header;
    struct EX_PROCESS *Owner;
} EX_STDOUT_SERVICE;

typedef struct EX_WAITABLE_OBJECT
{
    EX_OBJECT_HEADER Header;
    struct EX_PROCESS *Owner;
    void *Dispatcher;
    struct KTHREAD *CompanionThread;
} EX_WAITABLE_OBJECT;

struct EX_PROCESS
{
    EX_OBJECT_HEADER Header;
    KE_PROCESS_ADDRESS_SPACE AddressSpace;
    struct KE_USER_BOOTSTRAP_STAGING *Staging;
    EX_HANDLE SelfHandle;
    EX_HANDLE StdoutHandle;
    EX_HANDLE WaitHandle;
    uint32_t ProcessId;
    uint32_t ParentProcessId;
    uint32_t MainThreadId;
    EX_PROCESS_STATE State;
    uint32_t ProgramId;
    EX_STDOUT_SERVICE StdoutService;
    EX_WAITABLE_OBJECT WaitObject;
    EX_HANDLE_TABLE HandleTable;
};

struct EX_THREAD
{
    EX_OBJECT_HEADER Header;
    struct KTHREAD *Thread;
    EX_PROCESS *Process;
    EX_HANDLE SelfHandle;
};

void ExBootstrapInitializeObjectHeader(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE type);
HO_STATUS ExBootstrapRetainObject(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType);
HO_STATUS ExBootstrapReleaseObject(EX_OBJECT_HEADER *header,
                                   EX_OBJECT_TYPE expectedType,
                                   uint32_t *remainingReferences);
void ExBootstrapInitializeStdoutServiceObject(EX_PROCESS *process);
HO_STATUS ExBootstrapReleaseStdoutServiceOwner(EX_PROCESS *process);
void ExBootstrapInitializeWaitableObject(EX_PROCESS *process);
HO_STATUS ExBootstrapReleaseWaitableObjectOwner(EX_PROCESS *process);
HO_STATUS ExBootstrapCleanupWaitableBacking(EX_WAITABLE_OBJECT *waitObject);
void ExBootstrapInitializePrivateHandleTable(EX_PRIVATE_HANDLE_TABLE *table);
void ExBootstrapInitializeProcessObject(EX_PROCESS *process);
void ExBootstrapInitializeThreadObject(EX_THREAD *thread);
HO_STATUS ExBootstrapTeardownProcessPayload(EX_PROCESS *process);
EX_PROCESS *ExBootstrapRetainProcess(EX_PROCESS *process);
HO_STATUS ExBootstrapReleaseProcess(EX_PROCESS *process);
HO_STATUS ExBootstrapReleaseThread(EX_THREAD *thread);
HO_STATUS ExBootstrapInsertPrivateHandle(EX_PROCESS *process,
                                         EX_OBJECT_HEADER *objectHeader,
                                         EX_PRIVATE_HANDLE_RIGHTS rights,
                                         EX_PRIVATE_HANDLE *outHandle);
HO_STATUS ExBootstrapResolvePrivateHandle(EX_PROCESS *process,
                                          EX_PRIVATE_HANDLE handle,
                                          EX_OBJECT_TYPE expectedType,
                                          EX_PRIVATE_HANDLE_RIGHTS desiredRights,
                                          EX_OBJECT_HEADER **outObjectHeader);
HO_STATUS ExBootstrapReleaseResolvedObject(EX_OBJECT_HEADER *objectHeader);
HO_STATUS ExBootstrapClosePrivateHandle(EX_PROCESS *process, EX_PRIVATE_HANDLE *handle);
HO_STATUS ExBootstrapCloseAllPrivateHandles(EX_PROCESS *process);
BOOL ExBootstrapHasRuntimeAlias(void);
BOOL ExBootstrapIsRuntimeAliasObject(const EX_OBJECT_HEADER *objectHeader);
BOOL ExBootstrapRuntimeAliasMatchesProcess(const EX_PROCESS *process);
HO_STATUS ExBootstrapCaptureThreadList(EX_SYSINFO_THREAD_LIST *outThreadList);
HO_STATUS ExBootstrapCaptureProcessList(EX_SYSINFO_PROCESS_LIST *outProcessList);
HO_STATUS ExBootstrapPublishRuntimeAlias(EX_PROCESS *process, EX_THREAD *thread);
EX_THREAD *ExBootstrapLookupRuntimeThread(const struct KTHREAD *thread);
EX_PROCESS *ExBootstrapLookupRuntimeProcess(const struct KTHREAD *thread);
EX_PROCESS *ExBootstrapLookupRuntimeProcessByPid(uint32_t processId);
void ExBootstrapUnpublishRuntimeAlias(const struct KTHREAD *thread, EX_THREAD **outThread, EX_PROCESS **outProcess);
HO_STATUS ExBootstrapBuildInitialConstBytes(const EX_BOOTSTRAP_PROCESS_CREATE_PARAMS *params,
                                            uint8_t **outConstBytes,
                                            uint64_t *outConstLength);
HO_STATUS ExBootstrapPatchCapabilitySeed(EX_PROCESS *process, EX_THREAD *thread);
int64_t ExBootstrapHandleQuerySysinfo(EX_PROCESS *process, uint64_t infoClassRaw, uint64_t userBuffer, uint64_t length);
