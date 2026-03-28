/**
 * HimuOperatingSystem
 *
 * File: demo/irql.c
 * Description: IRQL and critical-section demo routines.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

void
RunIrqlSelfTest(void)
{
    KE_IRQL_GUARD outerGuard = {0};
    KE_IRQL_GUARD innerGuard = {0};
    KE_CRITICAL_SECTION outerCriticalSection = {0};
    KE_CRITICAL_SECTION innerCriticalSection = {0};

    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_PASSIVE_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(KeIsBlockingAllowed(), EC_INVALID_STATE);

    KeAcquireIrqlGuard(&outerGuard, KE_IRQL_DISPATCH_LEVEL);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(!KeIsBlockingAllowed(), EC_INVALID_STATE);

    KeAcquireIrqlGuard(&innerGuard, KE_IRQL_DISPATCH_LEVEL);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(!KeIsBlockingAllowed(), EC_INVALID_STATE);
    KeReleaseIrqlGuard(&innerGuard);

    KeEnterCriticalSection(&outerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    KeLeaveCriticalSection(&outerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);

    KeReleaseIrqlGuard(&outerGuard);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_PASSIVE_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(KeIsBlockingAllowed(), EC_INVALID_STATE);

    KeEnterCriticalSection(&outerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(!KeIsBlockingAllowed(), EC_INVALID_STATE);

    KeEnterCriticalSection(&innerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    KeLeaveCriticalSection(&innerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);

    KeLeaveCriticalSection(&outerCriticalSection);
    HO_KASSERT(KeGetCurrentIrql() == KE_IRQL_PASSIVE_LEVEL, EC_INVALID_STATE);
    HO_KASSERT(KeIsBlockingAllowed(), EC_INVALID_STATE);

    klog(KLOG_LEVEL_INFO, "[DEMO] IRQL/critical-section self-test passed\n");
}
