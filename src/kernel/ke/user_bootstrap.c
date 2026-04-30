/**
 * HimuOperatingSystem
 *
 * File: ke/user_bootstrap.c
 * Description: Minimal staged user-mode bootstrap mapping and first-entry helpers.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <arch/amd64/pm.h>
#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ex/user_bringup_sentinel_abi.h>
#include <kernel/ex/user_image_abi.h>
#include <kernel/ex/user_regression_anchors.h>
#include <kernel/ke/mm.h>
#include <kernel/ke/scheduler.h>
#include <kernel/ke/user_bootstrap.h>
#include <kernel/hodbg.h>
#include <libc/string.h>

#define KE_USER_BOOTSTRAP_MAX_MAPPINGS 3U
#define KE_USER_BOOTSTRAP_INITIAL_RFLAGS 0x202ULL

typedef enum KE_USER_BOOTSTRAP_MAPPING_KIND
{
    KE_USER_BOOTSTRAP_MAPPING_KIND_CODE = 0,
    KE_USER_BOOTSTRAP_MAPPING_KIND_CONST,
    KE_USER_BOOTSTRAP_MAPPING_KIND_STACK,
} KE_USER_BOOTSTRAP_MAPPING_KIND;

typedef struct KE_USER_BOOTSTRAP_MAPPING_RECORD
{
    KE_USER_BOOTSTRAP_MAPPING_KIND Kind;
    HO_VIRTUAL_ADDRESS VirtualBase;
    HO_PHYSICAL_ADDRESS PhysicalBase;
    uint64_t Attributes;
} KE_USER_BOOTSTRAP_MAPPING_RECORD;

struct KE_USER_BOOTSTRAP_STAGING
{
    HO_VIRTUAL_ADDRESS EntryPoint;
    HO_VIRTUAL_ADDRESS StackBase;
    HO_VIRTUAL_ADDRESS StackTop;
    HO_VIRTUAL_ADDRESS GuardBase;
    HO_VIRTUAL_ADDRESS PhaseOneMailboxAddress;
    uint32_t PhaseOneUserTimerHitCount;
    BOOL PhaseOneFirstEntryObserved;
    BOOL PhaseOneGateArmed;
    uint32_t MappingCount;
    HO_PHYSICAL_ADDRESS OwnerRootPageTablePhys;
    KTHREAD *AttachedThread;
    KE_USER_BOOTSTRAP_MAPPING_RECORD Mappings[KE_USER_BOOTSTRAP_MAX_MAPPINGS];
};

extern HO_NORETURN void KiUserBootstrapIretq(HO_VIRTUAL_ADDRESS userRip,
                                             HO_VIRTUAL_ADDRESS userRsp,
                                             uint64_t userRflags,
                                             uint64_t userCs,
                                             uint64_t userSs);

static void KiBuildProcessRootView(const KE_PROCESS_ADDRESS_SPACE *procSpace, KE_KERNEL_ADDRESS_SPACE *outView);
static HO_STATUS KiValidateCreateParams(const KE_USER_BOOTSTRAP_CREATE_PARAMS *params);
static HO_STATUS KiValidateBootstrapHole(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr);
static HO_STATUS KiPopulatePhysicalPage(HO_PHYSICAL_ADDRESS physAddr, const void *bytes, uint64_t byteCount);
static HO_STATUS KiRecordMappedPage(KE_USER_BOOTSTRAP_STAGING *staging,
                                    KE_USER_BOOTSTRAP_MAPPING_KIND kind,
                                    HO_VIRTUAL_ADDRESS virtAddr,
                                    HO_PHYSICAL_ADDRESS physAddr,
                                    uint64_t attributes);
static HO_STATUS KiAllocateAndMapUserPage(const KE_KERNEL_ADDRESS_SPACE *space,
                                          KE_USER_BOOTSTRAP_STAGING *staging,
                                          KE_USER_BOOTSTRAP_MAPPING_KIND kind,
                                          HO_VIRTUAL_ADDRESS virtAddr,
                                          uint64_t attributes,
                                          const void *bytes,
                                          uint64_t byteCount);
static const KE_USER_BOOTSTRAP_MAPPING_RECORD *KiFindMappedPage(const KE_USER_BOOTSTRAP_STAGING *staging,
                                                                KE_USER_BOOTSTRAP_MAPPING_KIND kind);
static KE_USER_BOOTSTRAP_STAGING *KiGetCurrentThreadStaging(void);

static void
KiBuildProcessRootView(const KE_PROCESS_ADDRESS_SPACE *procSpace, KE_KERNEL_ADDRESS_SPACE *outView)
{
    memset(outView, 0, sizeof(*outView));
    outView->RootPageTablePhys = procSpace->RootPageTablePhys;
    outView->Initialized = procSpace->Initialized;
}

static HO_STATUS
KiValidateCreateParams(const KE_USER_BOOTSTRAP_CREATE_PARAMS *params)
{
    if (!params)
        return EC_ILLEGAL_ARGUMENT;
    if (!params->CodeBytes || params->CodeLength == 0 || params->CodeLength > EX_USER_IMAGE_PAGE_SIZE)
        return EC_ILLEGAL_ARGUMENT;
    if (params->EntryOffset >= params->CodeLength)
        return EC_ILLEGAL_ARGUMENT;
    if ((params->ConstBytes == NULL) != (params->ConstLength == 0))
        return EC_ILLEGAL_ARGUMENT;
    if (params->ConstLength > EX_USER_IMAGE_PAGE_SIZE)
        return EC_ILLEGAL_ARGUMENT;

    return EC_SUCCESS;
}

static HO_STATUS
KiValidateBootstrapHole(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr)
{
    if (!space)
        return EC_ILLEGAL_ARGUMENT;

    if (KeFindImportedRegion(space, virtAddr) != NULL)
        return EC_INVALID_STATE;

    KE_PT_MAPPING mapping;
    HO_STATUS status = KePtQueryPage(space, virtAddr, &mapping);
    if (status != EC_SUCCESS)
        return status;

    return mapping.Present ? EC_INVALID_STATE : EC_SUCCESS;
}

static HO_STATUS
KiPopulatePhysicalPage(HO_PHYSICAL_ADDRESS physAddr, const void *bytes, uint64_t byteCount)
{
    KE_TEMP_PHYS_MAP_HANDLE handle = {0};
    HO_VIRTUAL_ADDRESS tempVirt = 0;
    HO_STATUS status = KeTempPhysMapAcquire(physAddr, PTE_WRITABLE | PTE_NO_EXECUTE, &handle, &tempVirt);
    if (status != EC_SUCCESS)
        return status;

    void *tempBuffer = (void *)(uint64_t)tempVirt;
    memset(tempBuffer, 0, EX_USER_IMAGE_PAGE_SIZE);
    if (bytes && byteCount != 0)
    {
        memcpy(tempBuffer, bytes, (size_t)byteCount);
    }

    HO_STATUS releaseStatus = KeTempPhysMapRelease(&handle);
    if (releaseStatus != EC_SUCCESS)
        return releaseStatus;

    return EC_SUCCESS;
}

static HO_STATUS
KiRecordMappedPage(KE_USER_BOOTSTRAP_STAGING *staging,
                   KE_USER_BOOTSTRAP_MAPPING_KIND kind,
                   HO_VIRTUAL_ADDRESS virtAddr,
                   HO_PHYSICAL_ADDRESS physAddr,
                   uint64_t attributes)
{
    if (!staging)
        return EC_ILLEGAL_ARGUMENT;
    if (staging->MappingCount >= KE_USER_BOOTSTRAP_MAX_MAPPINGS)
        return EC_INVALID_STATE;

    KE_USER_BOOTSTRAP_MAPPING_RECORD *record = &staging->Mappings[staging->MappingCount++];
    record->Kind = kind;
    record->VirtualBase = virtAddr;
    record->PhysicalBase = physAddr;
    record->Attributes = attributes;
    return EC_SUCCESS;
}

static HO_STATUS
KiAllocateAndMapUserPage(const KE_KERNEL_ADDRESS_SPACE *space,
                        KE_USER_BOOTSTRAP_STAGING *staging,
                        KE_USER_BOOTSTRAP_MAPPING_KIND kind,
                        HO_VIRTUAL_ADDRESS virtAddr,
                        uint64_t attributes,
                        const void *bytes,
                        uint64_t byteCount)
{
    HO_PHYSICAL_ADDRESS physAddr = 0;
    HO_STATUS status = KePmmAllocPages(1, NULL, &physAddr);
    if (status != EC_SUCCESS)
        return status;

    status = KiPopulatePhysicalPage(physAddr, bytes, byteCount);
    if (status != EC_SUCCESS)
    {
        (void)KePmmFreePages(physAddr, 1);
        return status;
    }

    status = KePtMapPage(space, virtAddr, physAddr, attributes);
    if (status != EC_SUCCESS)
    {
        (void)KePmmFreePages(physAddr, 1);
        return status;
    }

    status = KiRecordMappedPage(staging, kind, virtAddr, physAddr, attributes);
    if (status != EC_SUCCESS)
    {
        HO_STATUS cleanupStatus = KePtUnmapPage(space, virtAddr);
        if (cleanupStatus != EC_SUCCESS && cleanupStatus != EC_INVALID_STATE)
        {
            return cleanupStatus;
        }
        (void)KePmmFreePages(physAddr, 1);
        return status;
    }

    return EC_SUCCESS;
}

static const KE_USER_BOOTSTRAP_MAPPING_RECORD *
KiFindMappedPage(const KE_USER_BOOTSTRAP_STAGING *staging, KE_USER_BOOTSTRAP_MAPPING_KIND kind)
{
    if (!staging)
        return NULL;

    for (uint32_t index = 0; index < staging->MappingCount; ++index)
    {
        const KE_USER_BOOTSTRAP_MAPPING_RECORD *record = &staging->Mappings[index];
        if (record->Kind == kind)
            return record;
    }

    return NULL;
}

static KE_USER_BOOTSTRAP_STAGING *
KiGetCurrentThreadStaging(void)
{
    KTHREAD *thread = KeGetCurrentThread();
    if (!thread)
        return NULL;

    KE_USER_BOOTSTRAP_STAGING *staging = ExBootstrapAdapterQueryThreadStaging(thread);
    if (staging == NULL)
        return NULL;

    if (staging->AttachedThread != thread)
        return NULL;

    return staging;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserBootstrapCreateStaging(const KE_USER_BOOTSTRAP_CREATE_PARAMS *params,
                             KE_PROCESS_ADDRESS_SPACE *targetSpace,
                             KE_USER_BOOTSTRAP_STAGING **outStaging)
{
    HO_STATUS destroyStatus = EC_SUCCESS;
    KE_KERNEL_ADDRESS_SPACE processView = {0};

    if (!outStaging)
        return EC_ILLEGAL_ARGUMENT;

    *outStaging = NULL;

    if (targetSpace == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!targetSpace->Initialized)
        return EC_INVALID_STATE;

    HO_STATUS status = KiValidateCreateParams(params);
    if (status != EC_SUCCESS)
        return status;

    KiBuildProcessRootView(targetSpace, &processView);

    const KE_KERNEL_ADDRESS_SPACE *space = &processView;

    status = KiValidateBootstrapHole(space, 0);
    if (status != EC_SUCCESS)
        return status;

    status = KiValidateBootstrapHole(space, EX_USER_IMAGE_CODE_BASE);
    if (status != EC_SUCCESS)
        return status;

    status = KiValidateBootstrapHole(space, EX_USER_IMAGE_CONST_BASE);
    if (status != EC_SUCCESS)
        return status;

    status = KiValidateBootstrapHole(space, EX_USER_IMAGE_STACK_GUARD_BASE);
    if (status != EC_SUCCESS)
        return status;

    status = KiValidateBootstrapHole(space, EX_USER_IMAGE_STACK_BASE);
    if (status != EC_SUCCESS)
        return status;

    KE_USER_BOOTSTRAP_STAGING *staging = (KE_USER_BOOTSTRAP_STAGING *)kzalloc(sizeof(*staging));
    if (!staging)
        return EC_OUT_OF_RESOURCE;

    staging->EntryPoint = EX_USER_IMAGE_CODE_BASE + params->EntryOffset;
    staging->StackBase = EX_USER_IMAGE_STACK_BASE;
    staging->StackTop = EX_USER_IMAGE_STACK_TOP;
    staging->GuardBase = EX_USER_IMAGE_STACK_GUARD_BASE;
    staging->PhaseOneMailboxAddress = EX_USER_BRINGUP_P1_MAILBOX_ADDRESS;
    staging->OwnerRootPageTablePhys = targetSpace->RootPageTablePhys;

    status = KiAllocateAndMapUserPage(space,
                                      staging,
                                      KE_USER_BOOTSTRAP_MAPPING_KIND_CODE,
                                      EX_USER_IMAGE_CODE_BASE,
                                      PTE_USER,
                                      params->CodeBytes,
                                      params->CodeLength);
    if (status != EC_SUCCESS)
        goto cleanup;

    if (params->ConstLength != 0)
    {
        status = KiAllocateAndMapUserPage(space,
                                          staging,
                                          KE_USER_BOOTSTRAP_MAPPING_KIND_CONST,
                                          EX_USER_IMAGE_CONST_BASE,
                                          PTE_USER | PTE_NO_EXECUTE,
                                          params->ConstBytes,
                                          params->ConstLength);
        if (status != EC_SUCCESS)
            goto cleanup;
    }

    status = KiAllocateAndMapUserPage(space,
                                      staging,
                                      KE_USER_BOOTSTRAP_MAPPING_KIND_STACK,
                                      EX_USER_IMAGE_STACK_BASE,
                                      PTE_USER | PTE_WRITABLE | PTE_NO_EXECUTE,
                                      NULL,
                                      0);
    if (status != EC_SUCCESS)
        goto cleanup;

    status = KiValidateBootstrapHole(space, staging->GuardBase);
    if (status != EC_SUCCESS)
        goto cleanup;

    const KE_USER_BOOTSTRAP_MAPPING_RECORD *stackRecord =
        KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_STACK);
    HO_KASSERT(stackRecord != NULL, EC_INVALID_STATE);

    KE_TEMP_PHYS_MAP_HANDLE mailboxHandle = {0};
    HO_VIRTUAL_ADDRESS mailboxTempVirt = 0;
    status = KeTempPhysMapAcquire(stackRecord->PhysicalBase,
                                  PTE_WRITABLE | PTE_NO_EXECUTE,
                                  &mailboxHandle,
                                  &mailboxTempVirt);
    if (status != EC_SUCCESS)
        goto cleanup;

    *(volatile uint32_t *)(uint64_t)(mailboxTempVirt + EX_USER_BRINGUP_P1_MAILBOX_OFFSET) =
        EX_USER_BRINGUP_P1_MAILBOX_CLOSED;

    HO_STATUS mailboxRelease = KeTempPhysMapRelease(&mailboxHandle);
    if (mailboxRelease != EC_SUCCESS)
    {
        status = mailboxRelease;
        goto cleanup;
    }

    *outStaging = staging;
    return EC_SUCCESS;

cleanup:
    destroyStatus = KeUserBootstrapDestroyStaging(staging);
    if (destroyStatus != EC_SUCCESS)
        return destroyStatus;
    return status;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserBootstrapDestroyStaging(KE_USER_BOOTSTRAP_STAGING *staging)
{
    if (!staging)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS firstError = EC_SUCCESS;
    KE_KERNEL_ADDRESS_SPACE ownerView = {0};
    ownerView.RootPageTablePhys = staging->OwnerRootPageTablePhys;
    ownerView.Initialized = (staging->OwnerRootPageTablePhys != 0);
    const KE_KERNEL_ADDRESS_SPACE *space = &ownerView;

    if (staging->AttachedThread != NULL)
    {
        staging->AttachedThread = NULL;
    }

    while (staging->MappingCount != 0)
    {
        KE_USER_BOOTSTRAP_MAPPING_RECORD *record = &staging->Mappings[staging->MappingCount - 1];
        BOOL canFreeBackingPage = FALSE;

        if (space && space->Initialized)
        {
            HO_STATUS unmapStatus = KePtUnmapPage(space, record->VirtualBase);
            if (unmapStatus == EC_SUCCESS || unmapStatus == EC_INVALID_STATE)
            {
                canFreeBackingPage = TRUE;
            }
            else if (firstError == EC_SUCCESS)
            {
                firstError = unmapStatus;
            }
        }
        else if (firstError == EC_SUCCESS)
        {
            firstError = EC_INVALID_STATE;
        }

        if (canFreeBackingPage)
        {
            HO_STATUS freeStatus = KePmmFreePages(record->PhysicalBase, 1);
            if (freeStatus != EC_SUCCESS && firstError == EC_SUCCESS)
            {
                firstError = freeStatus;
            }
        }

        memset(record, 0, sizeof(*record));
        --staging->MappingCount;
    }

    kfree(staging);
    return firstError;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserBootstrapAttachThread(KTHREAD *thread, KE_USER_BOOTSTRAP_STAGING *staging)
{
    if (!thread || !staging)
        return EC_ILLEGAL_ARGUMENT;
    if ((thread->Flags & KTHREAD_FLAG_IDLE) != 0)
        return EC_INVALID_STATE;
    if (thread->State != KTHREAD_STATE_NEW)
        return EC_INVALID_STATE;
    if (staging->AttachedThread != NULL && staging->AttachedThread != thread)
        return EC_INVALID_STATE;
    if (KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_CODE) == NULL)
        return EC_INVALID_STATE;
    if (KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_STACK) == NULL)
        return EC_INVALID_STATE;

    staging->AttachedThread = thread;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserBootstrapDetachThread(KTHREAD *thread, KE_USER_BOOTSTRAP_STAGING *staging)
{
    if (!thread || !staging)
        return EC_ILLEGAL_ARGUMENT;
    if (staging->AttachedThread == NULL)
        return EC_SUCCESS;
    if (staging->AttachedThread != thread)
        return EC_INVALID_STATE;

    staging->AttachedThread = NULL;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserBootstrapPatchConstBytes(KE_USER_BOOTSTRAP_STAGING *staging,
                               uint64_t offset,
                               const void *bytes,
                               uint64_t length)
{
    if (staging == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (length == 0)
        return EC_SUCCESS;

    if (bytes == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (offset > EX_USER_IMAGE_PAGE_SIZE || length > EX_USER_IMAGE_PAGE_SIZE)
        return EC_ILLEGAL_ARGUMENT;

    if ((offset + length) < offset || (offset + length) > EX_USER_IMAGE_PAGE_SIZE)
        return EC_ILLEGAL_ARGUMENT;

    const KE_USER_BOOTSTRAP_MAPPING_RECORD *constRecord =
        KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_CONST);
    if (constRecord == NULL)
        return EC_INVALID_STATE;

    KE_TEMP_PHYS_MAP_HANDLE patchHandle = {0};
    HO_VIRTUAL_ADDRESS patchTempVirt = 0;
    HO_STATUS status = KeTempPhysMapAcquire(constRecord->PhysicalBase,
                                            PTE_WRITABLE | PTE_NO_EXECUTE,
                                            &patchHandle,
                                            &patchTempVirt);
    if (status != EC_SUCCESS)
        return status;

    memcpy((void *)(uint64_t)(patchTempVirt + offset), bytes, (size_t)length);

    return KeTempPhysMapRelease(&patchHandle);
}

HO_KERNEL_API HO_NODISCARD HO_STATUS
KeUserBootstrapQueryCurrentThreadLayout(KE_USER_BOOTSTRAP_LAYOUT *outLayout)
{
    if (!outLayout)
        return EC_ILLEGAL_ARGUMENT;

    memset(outLayout, 0, sizeof(*outLayout));

    KE_USER_BOOTSTRAP_STAGING *staging = KiGetCurrentThreadStaging();
    if (staging == NULL)
        return EC_INVALID_STATE;

    const KE_USER_BOOTSTRAP_MAPPING_RECORD *codeRecord =
        KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_CODE);
    const KE_USER_BOOTSTRAP_MAPPING_RECORD *stackRecord =
        KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_STACK);
    if (codeRecord == NULL || stackRecord == NULL)
        return EC_INVALID_STATE;

    if (staging->OwnerRootPageTablePhys == 0)
        return EC_INVALID_STATE;

    if (staging->StackTop <= codeRecord->VirtualBase)
        return EC_INVALID_STATE;

    if (stackRecord->VirtualBase != staging->StackBase)
        return EC_INVALID_STATE;

    outLayout->UserRangeBase = codeRecord->VirtualBase;
    outLayout->UserRangeEndExclusive = staging->StackTop;
    outLayout->EntryPoint = staging->EntryPoint;
    outLayout->GuardBase = staging->GuardBase;
    outLayout->StackBase = staging->StackBase;
    outLayout->StackTop = staging->StackTop;
    outLayout->PhaseOneMailboxAddress = staging->PhaseOneMailboxAddress;
    outLayout->OwnerRootPageTablePhys = staging->OwnerRootPageTablePhys;
    return EC_SUCCESS;
}

HO_KERNEL_API void
KeUserBootstrapObserveCurrentThreadUserTimerPreemption(void)
{
    KTHREAD *thread = KeGetCurrentThread();
    KE_USER_BOOTSTRAP_STAGING *staging = KiGetCurrentThreadStaging();
    if (!thread || !staging)
        return;

    if (!staging->PhaseOneFirstEntryObserved || staging->PhaseOneGateArmed)
        return;

    HO_KASSERT(KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_STACK) != NULL, EC_INVALID_STATE);
    HO_KASSERT(staging->PhaseOneMailboxAddress == EX_USER_BRINGUP_P1_MAILBOX_ADDRESS, EC_INVALID_STATE);

    if (staging->PhaseOneUserTimerHitCount < 2)
    {
        ++staging->PhaseOneUserTimerHitCount;
    }

    klog(KLOG_LEVEL_INFO,
         EX_USER_REGRESSION_LOG_TIMER_FROM_USER_FORMAT " thread=%u\n",
         staging->PhaseOneUserTimerHitCount,
         thread->ThreadId);

    if (staging->PhaseOneUserTimerHitCount >= 2)
    {
        *(volatile uint32_t *)(uint64_t)staging->PhaseOneMailboxAddress = EX_USER_BRINGUP_P1_MAILBOX_GATE_OPEN;
        staging->PhaseOneGateArmed = TRUE;

        klog(KLOG_LEVEL_INFO,
             EX_USER_REGRESSION_LOG_P1_GATE_ARMED " thread=%u mailbox=%p\n",
             thread->ThreadId,
             (void *)(uint64_t)staging->PhaseOneMailboxAddress);
    }
}

HO_KERNEL_API HO_NORETURN void
KeUserBootstrapEnterCurrentThread(void)
{
    KTHREAD *thread = KeGetCurrentThread();
    HO_KASSERT(thread != NULL, EC_INVALID_STATE);

    KE_USER_BOOTSTRAP_STAGING *staging = KiGetCurrentThreadStaging();
    HO_KASSERT(staging != NULL, EC_INVALID_STATE);
    HO_KASSERT(staging->AttachedThread == thread, EC_INVALID_STATE);
    HO_KASSERT(KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_CODE) != NULL, EC_INVALID_STATE);
    HO_KASSERT(KiFindMappedPage(staging, KE_USER_BOOTSTRAP_MAPPING_KIND_STACK) != NULL, EC_INVALID_STATE);
    HO_KASSERT(staging->EntryPoint >= EX_USER_IMAGE_CODE_BASE, EC_INVALID_STATE);
    HO_KASSERT(staging->EntryPoint < (EX_USER_IMAGE_CODE_BASE + EX_USER_IMAGE_PAGE_SIZE), EC_INVALID_STATE);
    HO_KASSERT(staging->StackBase == EX_USER_IMAGE_STACK_BASE, EC_INVALID_STATE);
    HO_KASSERT(staging->StackTop == EX_USER_IMAGE_STACK_TOP, EC_INVALID_STATE);
    HO_KASSERT(staging->PhaseOneMailboxAddress == EX_USER_BRINGUP_P1_MAILBOX_ADDRESS, EC_INVALID_STATE);

    staging->PhaseOneFirstEntryObserved = TRUE;

    klog(KLOG_LEVEL_INFO, EX_USER_REGRESSION_LOG_P1_FIRST_ENTRY "\n");

    KiUserBootstrapIretq(
        staging->EntryPoint, staging->StackTop, KE_USER_BOOTSTRAP_INITIAL_RFLAGS, GDT_USER_CODE_SEL, GDT_USER_DATA_SEL);
    __builtin_unreachable();
}
