/**
 * HimuOperatingSystem
 *
 * File: ke/irql.h
 * Description:
 * Ke Layer - Minimal IRQL / execution-level model for UP scheduling paths.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <arch/arch.h>

typedef enum KE_IRQL
{
    KE_IRQL_PASSIVE_LEVEL = 0,
    KE_IRQL_DISPATCH_LEVEL = 1,
} KE_IRQL;

typedef struct KE_IRQL_STATE
{
    KE_IRQL CurrentLevel;
    uint32_t DispatchDepth;
    uint32_t InterruptDepth;
} KE_IRQL_STATE;

typedef struct KE_IRQL_GUARD
{
    BOOL Active;
    BOOL Transitioned;
    ARCH_INTERRUPT_STATE SavedInterruptState;
    KE_IRQL PreviousLevel;
    KE_IRQL TargetLevel;
    uint32_t EnterDepth;
} KE_IRQL_GUARD;

HO_KERNEL_API void KeInitializeIrqlState(KE_IRQL_STATE *state);
HO_KERNEL_API void KeSetCurrentIrqlState(KE_IRQL_STATE *state);

HO_KERNEL_API KE_IRQL KeGetCurrentIrql(void);
HO_KERNEL_API BOOL KeIsBlockingAllowed(void);

HO_KERNEL_API void KeAcquireIrqlGuard(KE_IRQL_GUARD *guard, KE_IRQL targetLevel);
HO_KERNEL_API void KeReleaseIrqlGuard(KE_IRQL_GUARD *guard);

HO_KERNEL_API void KeEnterInterruptContext(void);
HO_KERNEL_API void KeLeaveInterruptContext(void);
