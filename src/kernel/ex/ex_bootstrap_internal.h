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
struct EX_PROCESS;

typedef enum EX_OBJECT_TYPE
{
    EX_OBJECT_TYPE_INVALID = 0,
    EX_OBJECT_TYPE_PROCESS = 1,
    EX_OBJECT_TYPE_THREAD = 2,
    EX_OBJECT_TYPE_STDOUT_SERVICE = 3,
} EX_OBJECT_TYPE;

typedef struct EX_OBJECT_HEADER
{
    EX_OBJECT_TYPE Type;
    uint32_t ReferenceCount;
} EX_OBJECT_HEADER;

typedef uint32_t EX_PRIVATE_HANDLE;
typedef uint32_t EX_PRIVATE_HANDLE_RIGHTS;

#define EX_PRIVATE_HANDLE_INVALID            ((EX_PRIVATE_HANDLE)0u)
#define EX_PRIVATE_HANDLE_TABLE_CAPACITY     8u

#define EX_PRIVATE_HANDLE_RIGHT_NONE         ((EX_PRIVATE_HANDLE_RIGHTS)0u)
#define EX_PRIVATE_HANDLE_RIGHT_QUERY        ((EX_PRIVATE_HANDLE_RIGHTS)0x00000001u)
#define EX_PRIVATE_HANDLE_RIGHT_CLOSE        ((EX_PRIVATE_HANDLE_RIGHTS)0x00000002u)
#define EX_PRIVATE_HANDLE_RIGHT_PROCESS_SELF ((EX_PRIVATE_HANDLE_RIGHTS)0x00000004u)
#define EX_PRIVATE_HANDLE_RIGHT_THREAD_SELF  ((EX_PRIVATE_HANDLE_RIGHTS)0x00000008u)
#define EX_PRIVATE_HANDLE_RIGHT_WRITE        ((EX_PRIVATE_HANDLE_RIGHTS)0x00000010u)

typedef struct EX_STDOUT_SERVICE
{
    EX_OBJECT_HEADER Header;
    struct EX_PROCESS *Owner;
} EX_STDOUT_SERVICE;

typedef struct EX_PRIVATE_HANDLE_SLOT
{
    EX_OBJECT_HEADER *Object;
    EX_PRIVATE_HANDLE_RIGHTS Rights;
    uint32_t Generation;
} EX_PRIVATE_HANDLE_SLOT;

typedef struct EX_PRIVATE_HANDLE_TABLE
{
    EX_PRIVATE_HANDLE_SLOT Slots[EX_PRIVATE_HANDLE_TABLE_CAPACITY];
} EX_PRIVATE_HANDLE_TABLE;

struct EX_PROCESS
{
    EX_OBJECT_HEADER Header;
    KE_PROCESS_ADDRESS_SPACE AddressSpace;
    struct KE_USER_BOOTSTRAP_STAGING *Staging;
    EX_PRIVATE_HANDLE SelfHandle;
    EX_PRIVATE_HANDLE StdoutHandle;
    EX_STDOUT_SERVICE StdoutService;
    EX_PRIVATE_HANDLE_TABLE HandleTable;
};

struct EX_THREAD
{
    EX_OBJECT_HEADER Header;
    struct KTHREAD *Thread;
    EX_PROCESS *Process;
    EX_PRIVATE_HANDLE SelfHandle;
};

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
BOOL ExBootstrapRuntimeAliasMatchesProcess(const EX_PROCESS *process);
HO_STATUS ExBootstrapPublishRuntimeAlias(EX_PROCESS *process, EX_THREAD *thread);
EX_THREAD *ExBootstrapLookupRuntimeThread(const struct KTHREAD *thread);
EX_PROCESS *ExBootstrapLookupRuntimeProcess(const struct KTHREAD *thread);
void ExBootstrapUnpublishRuntimeAlias(EX_THREAD **outThread, EX_PROCESS **outProcess);

/* Bootstrap runtime registry stores non-owning identity aliases only. */
extern EX_PROCESS *gExBootstrapProcess;
extern EX_THREAD *gExBootstrapThread;
