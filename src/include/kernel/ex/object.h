/**
 * HimuOperatingSystem
 *
 * File: ex/object.h
 * Description: Executive Lite object header and reference interface.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

typedef enum EX_OBJECT_TYPE
{
    EX_OBJECT_TYPE_INVALID = 0,
    EX_OBJECT_TYPE_PROCESS = 1,
    EX_OBJECT_TYPE_THREAD = 2,
    EX_OBJECT_TYPE_CONSOLE = 3,
    EX_OBJECT_TYPE_STDOUT_SERVICE = EX_OBJECT_TYPE_CONSOLE,
} EX_OBJECT_TYPE;

typedef uint32_t EX_OBJECT_FLAGS;

#define EX_OBJECT_FLAG_NONE ((EX_OBJECT_FLAGS)0u)

struct EX_OBJECT_HEADER;

typedef HO_STATUS (*EX_OBJECT_DESTROY_ROUTINE)(struct EX_OBJECT_HEADER *header);

typedef struct EX_OBJECT_HEADER
{
    EX_OBJECT_TYPE Type;
    uint32_t ReferenceCount;
    EX_OBJECT_FLAGS Flags;
    EX_OBJECT_DESTROY_ROUTINE DestroyRoutine;
} EX_OBJECT_HEADER;

BOOL ExObjectIsValidType(EX_OBJECT_TYPE type);
void ExObjectInitializeHeader(EX_OBJECT_HEADER *header,
                              EX_OBJECT_TYPE type,
                              EX_OBJECT_FLAGS flags,
                              EX_OBJECT_DESTROY_ROUTINE destroyRoutine);
HO_STATUS ExObjectRetain(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType);
HO_STATUS ExObjectRelease(EX_OBJECT_HEADER *header, EX_OBJECT_TYPE expectedType, uint32_t *remainingReferences);
