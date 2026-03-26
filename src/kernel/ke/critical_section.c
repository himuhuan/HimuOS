/**
 * HimuOperatingSystem
 *
 * File: ke/critical_section.c
 * Description:
 * Ke Layer - UP critical section implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/critical_section.h>
#include <kernel/hodbg.h>

static uint32_t gCriticalSectionDepth;

HO_KERNEL_API void
KeEnterCriticalSection(KE_CRITICAL_SECTION *guard)
{
    HO_KASSERT(guard != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(!guard->Active, EC_INVALID_STATE);
    HO_KASSERT(gCriticalSectionDepth != 0xFFFFFFFFU, EC_OUT_OF_RESOURCE);

    KeAcquireIrqlGuard(&guard->IrqlGuard, KE_IRQL_DISPATCH_LEVEL);
    gCriticalSectionDepth++;

    guard->EnterDepth = gCriticalSectionDepth;
    guard->Active = TRUE;
}

HO_KERNEL_API void
KeLeaveCriticalSection(KE_CRITICAL_SECTION *guard)
{
    HO_KASSERT(guard != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(guard->Active, EC_INVALID_STATE);
    HO_KASSERT(gCriticalSectionDepth != 0, EC_INVALID_STATE);
    HO_KASSERT(guard->EnterDepth == gCriticalSectionDepth, EC_INVALID_STATE);

    gCriticalSectionDepth--;

    guard->Active = FALSE;
    guard->EnterDepth = 0;

    KeReleaseIrqlGuard(&guard->IrqlGuard);
}

HO_KERNEL_API uint32_t
KeGetCriticalSectionDepth(void)
{
    return gCriticalSectionDepth;
}
