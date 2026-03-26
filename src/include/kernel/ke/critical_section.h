/**
 * HimuOperatingSystem
 *
 * File: ke/critical_section.h
 * Description:
 * Ke Layer - UP critical section API.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <kernel/ke/irql.h>

typedef struct KE_CRITICAL_SECTION
{
    KE_IRQL_GUARD IrqlGuard;
    uint32_t EnterDepth;
    BOOL Active;
} KE_CRITICAL_SECTION;

HO_KERNEL_API void KeEnterCriticalSection(KE_CRITICAL_SECTION *guard);
HO_KERNEL_API void KeLeaveCriticalSection(KE_CRITICAL_SECTION *guard);
HO_KERNEL_API uint32_t KeGetCriticalSectionDepth(void);
