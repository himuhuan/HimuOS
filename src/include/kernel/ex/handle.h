/**
 * HimuOperatingSystem
 *
 * File: ex/handle.h
 * Description: Executive Lite process-owned handle table interface.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#include <kernel/ex/object.h>

struct EX_PROCESS;

typedef uint32_t EX_HANDLE;
typedef uint32_t EX_HANDLE_RIGHTS;

#define EX_HANDLE_INVALID            ((EX_HANDLE)0u)
#define EX_MAX_HANDLES_PER_PROCESS   32u

#define EX_HANDLE_RIGHT_NONE         ((EX_HANDLE_RIGHTS)0u)
#define EX_HANDLE_RIGHT_QUERY        ((EX_HANDLE_RIGHTS)0x00000001u)
#define EX_HANDLE_RIGHT_CLOSE        ((EX_HANDLE_RIGHTS)0x00000002u)
#define EX_HANDLE_RIGHT_PROCESS_SELF ((EX_HANDLE_RIGHTS)0x00000004u)
#define EX_HANDLE_RIGHT_THREAD_SELF  ((EX_HANDLE_RIGHTS)0x00000008u)
#define EX_HANDLE_RIGHT_WRITE        ((EX_HANDLE_RIGHTS)0x00000010u)
#define EX_HANDLE_RIGHT_WAIT         ((EX_HANDLE_RIGHTS)0x00000020u)

typedef struct EX_HANDLE_SLOT
{
    EX_OBJECT_HEADER *Object;
    EX_HANDLE_RIGHTS Rights;
    uint32_t Generation;
} EX_HANDLE_SLOT;

typedef struct EX_HANDLE_TABLE
{
    EX_HANDLE_SLOT Slots[EX_MAX_HANDLES_PER_PROCESS];
} EX_HANDLE_TABLE;

void ExHandleInitializeTable(EX_HANDLE_TABLE *table);
HO_STATUS ExHandleInsert(struct EX_PROCESS *process,
                         EX_OBJECT_HEADER *objectHeader,
                         EX_HANDLE_RIGHTS rights,
                         EX_HANDLE *outHandle);
HO_STATUS ExHandleResolve(struct EX_PROCESS *process,
                          EX_HANDLE handle,
                          EX_OBJECT_TYPE expectedType,
                          EX_HANDLE_RIGHTS desiredRights,
                          EX_OBJECT_HEADER **outObjectHeader);
HO_STATUS ExHandleReleaseResolvedObject(EX_OBJECT_HEADER *objectHeader);
HO_STATUS ExHandleClose(struct EX_PROCESS *process, EX_HANDLE *handle);
HO_STATUS ExHandleCloseAll(struct EX_PROCESS *process);
