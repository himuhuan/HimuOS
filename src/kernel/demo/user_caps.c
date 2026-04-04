/**
 * HimuOperatingSystem
 *
 * File: demo/user_caps.c
 * Description: Stage-1 bootstrap capability regression profile covering the
 *              versioned seed block, stdout capability write, stale-handle
 *              rejection after close, and the unchanged raw exit clean path.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_bootstrap.h>

#define KI_U32_BYTE(value, shift) ((uint8_t)((((uint32_t)(value)) >> (shift)) & 0xFFU))
#define KI_U32_LE_BYTES(value)    KI_U32_BYTE((value), 0), KI_U32_BYTE((value), 8), KI_U32_BYTE((value), 16), \
                                 KI_U32_BYTE((value), 24)

enum
{
    KI_USER_CAPS_PAYLOAD_ENTRY_OFFSET = 0U,
    KI_USER_CAPS_PAYLOAD_MESSAGE_OFFSET = (uint32_t)KE_USER_BOOTSTRAP_CONST_PAYLOAD_OFFSET,
};

static void KiUnexpectedUserCapsKernelEntry(void *arg);

static const char gKiUserCapsConstBytes[] = "[USERCAP] stdout capability write\n";

enum
{
    KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH = sizeof(gKiUserCapsConstBytes) - 1U,
    KI_USER_CAPS_PAYLOAD_EXPECTED_STALE_STATUS = (uint32_t)(-(int32_t)EC_INVALID_STATE),
};

static const uint8_t gKiUserCapsCodeBytes[] = {
    // Wait for the existing P1 gate, validate the versioned capability seed block,
    // emit one stdout capability write, close that handle, prove stale-handle
    // rejection, and then exit through the unchanged raw bootstrap exit path.
    0xB9, KI_U32_LE_BYTES((uint32_t)KE_USER_BOOTSTRAP_STACK_MAILBOX_ADDRESS),
    0x8B, 0x01,
    0x3D, KI_U32_LE_BYTES(KE_USER_BOOTSTRAP_P1_MAILBOX_GATE_OPEN),
    0x75, 0xF7,

    0xB9, KI_U32_LE_BYTES((uint32_t)KE_USER_BOOTSTRAP_CONST_BASE),
    0x8B, 0x01,
    0x3D, KI_U32_LE_BYTES(KE_USER_BOOTSTRAP_CAPABILITY_SEED_VERSION),
    0x0F, 0x85, KI_U32_LE_BYTES(161U),
    0x8B, 0x41, KE_USER_BOOTSTRAP_CAPABILITY_SEED_SIZE_OFFSET,
    0x3D, KI_U32_LE_BYTES(KE_USER_BOOTSTRAP_CAPABILITY_SEED_BLOCK_SIZE),
    0x0F, 0x85, KI_U32_LE_BYTES(147U),
    0x8B, 0x41, KE_USER_BOOTSTRAP_CAPABILITY_PROCESS_SELF_OFFSET,
    0x85, 0xC0,
    0x0F, 0x84, KI_U32_LE_BYTES(136U),
    0x8B, 0x41, KE_USER_BOOTSTRAP_CAPABILITY_THREAD_SELF_OFFSET,
    0x85, 0xC0,
    0x0F, 0x84, KI_U32_LE_BYTES(125U),
    0x8B, 0x41, KE_USER_BOOTSTRAP_CAPABILITY_STDOUT_OFFSET,
    0x85, 0xC0,
    0x0F, 0x84, KI_U32_LE_BYTES(114U),
    0x8B, 0x41, KE_USER_BOOTSTRAP_CAPABILITY_WAIT_OBJECT_OFFSET,
    0x85, 0xC0,
    0x0F, 0x85, KI_U32_LE_BYTES(103U),

    0xB8, KI_U32_LE_BYTES(SYS_WRITE),
    0x8B, 0x79, KE_USER_BOOTSTRAP_CAPABILITY_STDOUT_OFFSET,
    0xBE, KI_U32_LE_BYTES((uint32_t)(KE_USER_BOOTSTRAP_CONST_BASE + KI_USER_CAPS_PAYLOAD_MESSAGE_OFFSET)),
    0xBA, KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH),
    0xCD, KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
    0x3D, KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH),
    0x0F, 0x85, KI_U32_LE_BYTES(72U),

    0xB9, KI_U32_LE_BYTES((uint32_t)KE_USER_BOOTSTRAP_CONST_BASE),
    0xB8, KI_U32_LE_BYTES(SYS_CLOSE),
    0x8B, 0x79, KE_USER_BOOTSTRAP_CAPABILITY_STDOUT_OFFSET,
    0x31, 0xF6,
    0x31, 0xD2,
    0xCD, KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
    0x85, 0xC0,
    0x0F, 0x85, KI_U32_LE_BYTES(45U),

    0xB9, KI_U32_LE_BYTES((uint32_t)KE_USER_BOOTSTRAP_CONST_BASE),
    0xB8, KI_U32_LE_BYTES(SYS_WRITE),
    0x8B, 0x79, KE_USER_BOOTSTRAP_CAPABILITY_STDOUT_OFFSET,
    0xBE, KI_U32_LE_BYTES((uint32_t)(KE_USER_BOOTSTRAP_CONST_BASE + KI_USER_CAPS_PAYLOAD_MESSAGE_OFFSET)),
    0xBA, KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH),
    0xCD, KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
    0x3D, KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_EXPECTED_STALE_STATUS),
    0x0F, 0x85, KI_U32_LE_BYTES(9U),

    0xB8, KI_U32_LE_BYTES(SYS_RAW_EXIT),
    0x31, 0xFF,
    0xCD, KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
    0x0F, 0x0B,
};

static void
KiUnexpectedUserCapsKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "user_caps bootstrap thread unexpectedly executed the kernel entry point");
}

void
RunUserCapsDemo(void)
{
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS createParams = {
        .CodeBytes = gKiUserCapsCodeBytes,
        .CodeLength = sizeof(gKiUserCapsCodeBytes),
        .EntryOffset = KI_USER_CAPS_PAYLOAD_ENTRY_OFFSET,
        .ConstBytes = gKiUserCapsConstBytes,
        .ConstLength = KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH,
    };
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS threadParams = {
        .EntryPoint = KiUnexpectedUserCapsKernelEntry,
        .EntryArg = NULL,
    };
    EX_PROCESS *process = NULL;
    EX_THREAD *thread = NULL;

    HO_STATUS status = ExBootstrapCreateProcess(&createParams, &process);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create staged user_caps payload");

    status = ExBootstrapCreateThread(&process, &threadParams, &thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_caps bootstrap thread");

    status = ExBootstrapStartThread(&thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_caps bootstrap thread");
}

#undef KI_U32_LE_BYTES
#undef KI_U32_BYTE
