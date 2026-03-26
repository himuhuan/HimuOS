/**
 * HimuOperatingSystem
 *
 * File: ke/irql.c
 * Description:
 * Ke Layer - Minimal IRQL / execution-level bookkeeping for UP scheduling.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/irql.h>
#include <kernel/hodbg.h>

static KE_IRQL_STATE gBootstrapIrqlState = {
    .CurrentLevel = KE_IRQL_PASSIVE_LEVEL,
    .DispatchDepth = 0,
    .InterruptDepth = 0,
};

static KE_IRQL_STATE *gCurrentIrqlState = &gBootstrapIrqlState;

static KE_IRQL_STATE *
KiGetCurrentIrqlState(void)
{
    HO_KASSERT(gCurrentIrqlState != NULL, EC_INVALID_STATE);
    return gCurrentIrqlState;
}

static void
KiAssertIrqlState(const KE_IRQL_STATE *state)
{
    HO_KASSERT(state != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(state->InterruptDepth <= state->DispatchDepth, EC_INVALID_STATE);

    if (state->DispatchDepth == 0)
    {
        HO_KASSERT(state->CurrentLevel == KE_IRQL_PASSIVE_LEVEL, EC_INVALID_STATE);
    }
    else
    {
        HO_KASSERT(state->CurrentLevel == KE_IRQL_DISPATCH_LEVEL, EC_INVALID_STATE);
    }
}

HO_KERNEL_API void
KeInitializeIrqlState(KE_IRQL_STATE *state)
{
    HO_KASSERT(state != NULL, EC_ILLEGAL_ARGUMENT);

    state->CurrentLevel = KE_IRQL_PASSIVE_LEVEL;
    state->DispatchDepth = 0;
    state->InterruptDepth = 0;
}

HO_KERNEL_API void
KeSetCurrentIrqlState(KE_IRQL_STATE *state)
{
    HO_KASSERT(state != NULL, EC_ILLEGAL_ARGUMENT);
    KiAssertIrqlState(state);
    gCurrentIrqlState = state;
}

HO_KERNEL_API KE_IRQL
KeGetCurrentIrql(void)
{
    KE_IRQL_STATE *state = KiGetCurrentIrqlState();
    KiAssertIrqlState(state);
    return state->CurrentLevel;
}

HO_KERNEL_API BOOL
KeIsBlockingAllowed(void)
{
    return KeGetCurrentIrql() == KE_IRQL_PASSIVE_LEVEL;
}

HO_KERNEL_API void
KeAcquireIrqlGuard(KE_IRQL_GUARD *guard, KE_IRQL targetLevel)
{
    KE_IRQL_STATE *state = KiGetCurrentIrqlState();

    HO_KASSERT(guard != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(!guard->Active, EC_INVALID_STATE);
    HO_KASSERT(targetLevel == KE_IRQL_DISPATCH_LEVEL, EC_NOT_SUPPORTED);
    HO_KASSERT(state->DispatchDepth != 0xFFFFFFFFU, EC_OUT_OF_RESOURCE);

    KiAssertIrqlState(state);

    guard->SavedInterruptState = (ARCH_INTERRUPT_STATE){0};
    guard->PreviousLevel = state->CurrentLevel;
    guard->TargetLevel = targetLevel;
    guard->Transitioned = FALSE;

    if (state->CurrentLevel == KE_IRQL_PASSIVE_LEVEL)
    {
        guard->SavedInterruptState = ArchDisableInterrupts();
        guard->Transitioned = TRUE;
    }

    state->DispatchDepth++;
    state->CurrentLevel = KE_IRQL_DISPATCH_LEVEL;

    guard->EnterDepth = state->DispatchDepth;
    guard->Active = TRUE;

    KiAssertIrqlState(state);
}

HO_KERNEL_API void
KeReleaseIrqlGuard(KE_IRQL_GUARD *guard)
{
    KE_IRQL_STATE *state = KiGetCurrentIrqlState();

    HO_KASSERT(guard != NULL, EC_ILLEGAL_ARGUMENT);
    HO_KASSERT(guard->Active, EC_INVALID_STATE);
    HO_KASSERT(guard->TargetLevel == KE_IRQL_DISPATCH_LEVEL, EC_NOT_SUPPORTED);

    KiAssertIrqlState(state);
    HO_KASSERT(state->DispatchDepth != 0, EC_INVALID_STATE);
    HO_KASSERT(guard->EnterDepth == state->DispatchDepth, EC_INVALID_STATE);

    state->DispatchDepth--;
    state->CurrentLevel = state->DispatchDepth == 0 ? guard->PreviousLevel : KE_IRQL_DISPATCH_LEVEL;

    guard->Active = FALSE;
    guard->EnterDepth = 0;
    guard->TargetLevel = KE_IRQL_PASSIVE_LEVEL;

    if (guard->Transitioned)
    {
        HO_KASSERT(state->DispatchDepth == 0, EC_INVALID_STATE);
        ArchRestoreInterruptState(guard->SavedInterruptState);
    }

    guard->Transitioned = FALSE;

    KiAssertIrqlState(state);
}

HO_KERNEL_API void
KeEnterInterruptContext(void)
{
    KE_IRQL_STATE *state = KiGetCurrentIrqlState();

    KiAssertIrqlState(state);
    HO_KASSERT(state->DispatchDepth != 0xFFFFFFFFU, EC_OUT_OF_RESOURCE);
    HO_KASSERT(state->InterruptDepth != 0xFFFFFFFFU, EC_OUT_OF_RESOURCE);

    state->DispatchDepth++;
    state->InterruptDepth++;
    state->CurrentLevel = KE_IRQL_DISPATCH_LEVEL;

    KiAssertIrqlState(state);
}

HO_KERNEL_API void
KeLeaveInterruptContext(void)
{
    KE_IRQL_STATE *state = KiGetCurrentIrqlState();

    KiAssertIrqlState(state);
    HO_KASSERT(state->InterruptDepth != 0, EC_INVALID_STATE);
    HO_KASSERT(state->DispatchDepth != 0, EC_INVALID_STATE);
    HO_KASSERT(state->DispatchDepth == state->InterruptDepth, EC_INVALID_STATE);

    state->InterruptDepth--;
    state->DispatchDepth--;
    state->CurrentLevel = state->DispatchDepth == 0 ? KE_IRQL_PASSIVE_LEVEL : KE_IRQL_DISPATCH_LEVEL;

    KiAssertIrqlState(state);
}
