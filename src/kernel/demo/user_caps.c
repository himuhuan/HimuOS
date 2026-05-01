/**
 * HimuOperatingSystem
 *
 * File: demo/user_caps.c
 * Description: Stage-2 capability regression profile covering the
 *              versioned seed block, stdout capability write, process wait
 *              handle timeout/close, stale-handle rejection after close, and the
 *              unchanged raw exit clean path.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#include <kernel/ex/program.h>

#define KI_U32_BYTE(value, shift) ((uint8_t)((((uint32_t)(value)) >> (shift)) & 0xFFU))
#define KI_U32_LE_BYTES(value)                                                                                         \
    KI_U32_BYTE((value), 0), KI_U32_BYTE((value), 8), KI_U32_BYTE((value), 16), KI_U32_BYTE((value), 24)

enum
{
    KI_USER_CAPS_PAYLOAD_ENTRY_OFFSET = 0U,
    KI_USER_CAPS_PAYLOAD_MESSAGE_OFFSET = (uint32_t)EX_USER_IMAGE_CONST_PAYLOAD_OFFSET,
};

static const char gKiUserCapsConstBytes[] = "[USERCAP] stdout capability write\n";

enum
{
    KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH = sizeof(gKiUserCapsConstBytes) - 1U,
    KI_USER_CAPS_PAYLOAD_EXPECTED_STALE_STATUS = (uint32_t)(-(int32_t)EC_INVALID_STATE),
    KI_USER_CAPS_PAYLOAD_EXPECTED_WAIT_STATUS = (uint32_t)(-(int32_t)EC_TIMEOUT),
};

static const uint8_t gKiUserCapsCodeBytes[] = {
    // Wait for the existing P1 gate, validate the versioned capability seed block,
    // emit one stdout capability write, close that handle, prove stale-handle
    // rejection, poll the seeded process wait handle, close it, and then exit
    // through the raw runtime exit path.
    0xB9,
    KI_U32_LE_BYTES((uint32_t)EX_USER_BRINGUP_P1_MAILBOX_ADDRESS),
    0x8B,
    0x01,
    0x3D,
    KI_U32_LE_BYTES(EX_USER_BRINGUP_P1_MAILBOX_GATE_OPEN),
    0x75,
    0xF7,

    0xB9,
    KI_U32_LE_BYTES((uint32_t)EX_USER_IMAGE_CONST_BASE),
    0x8B,
    0x01,
    0x3D,
    KI_U32_LE_BYTES(EX_USER_CAPABILITY_SEED_VERSION),
    0x0F,
    0x85,
    KI_U32_LE_BYTES(218U),
    0x8B,
    0x41,
    EX_USER_CAPABILITY_SEED_SIZE_OFFSET,
    0x3D,
    KI_U32_LE_BYTES(EX_USER_CAPABILITY_SEED_BLOCK_SIZE),
    0x0F,
    0x85,
    KI_U32_LE_BYTES(204U),
    0x8B,
    0x41,
    EX_USER_CAPABILITY_PROCESS_SELF_OFFSET,
    0x85,
    0xC0,
    0x0F,
    0x84,
    KI_U32_LE_BYTES(193U),
    0x8B,
    0x41,
    EX_USER_CAPABILITY_THREAD_SELF_OFFSET,
    0x85,
    0xC0,
    0x0F,
    0x84,
    KI_U32_LE_BYTES(182U),
    0x8B,
    0x41,
    EX_USER_CAPABILITY_STDOUT_OFFSET,
    0x85,
    0xC0,
    0x0F,
    0x84,
    KI_U32_LE_BYTES(171U),
    0x8B,
    0x41,
    EX_USER_CAPABILITY_WAIT_OBJECT_OFFSET,
    0x85,
    0xC0,
    0x0F,
    0x84,
    KI_U32_LE_BYTES(160U),

    0xB8,
    KI_U32_LE_BYTES(EX_USER_SYS_WRITE),
    0x8B,
    0x79,
    EX_USER_CAPABILITY_STDOUT_OFFSET,
    0xBE,
    KI_U32_LE_BYTES((uint32_t)(EX_USER_IMAGE_CONST_BASE + KI_USER_CAPS_PAYLOAD_MESSAGE_OFFSET)),
    0xBA,
    KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH),
    0xCD,
    EX_USER_SYSCALL_VECTOR,
    0x3D,
    KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH),
    0x0F,
    0x85,
    KI_U32_LE_BYTES(129U),

    0xB9,
    KI_U32_LE_BYTES((uint32_t)EX_USER_IMAGE_CONST_BASE),
    0xB8,
    KI_U32_LE_BYTES(EX_USER_SYS_CLOSE),
    0x8B,
    0x79,
    EX_USER_CAPABILITY_STDOUT_OFFSET,
    0x31,
    0xF6,
    0x31,
    0xD2,
    0xCD,
    EX_USER_SYSCALL_VECTOR,
    0x85,
    0xC0,
    0x0F,
    0x85,
    KI_U32_LE_BYTES(102U),

    0xB9,
    KI_U32_LE_BYTES((uint32_t)EX_USER_IMAGE_CONST_BASE),
    0xB8,
    KI_U32_LE_BYTES(EX_USER_SYS_WRITE),
    0x8B,
    0x79,
    EX_USER_CAPABILITY_STDOUT_OFFSET,
    0xBE,
    KI_U32_LE_BYTES((uint32_t)(EX_USER_IMAGE_CONST_BASE + KI_USER_CAPS_PAYLOAD_MESSAGE_OFFSET)),
    0xBA,
    KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH),
    0xCD,
    EX_USER_SYSCALL_VECTOR,
    0x3D,
    KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_EXPECTED_STALE_STATUS),
    0x0F,
    0x85,
    KI_U32_LE_BYTES(66U),

    0xB9,
    KI_U32_LE_BYTES((uint32_t)EX_USER_IMAGE_CONST_BASE),
    0xB8,
    KI_U32_LE_BYTES(EX_USER_SYS_WAIT_ONE),
    0x8B,
    0x79,
    EX_USER_CAPABILITY_WAIT_OBJECT_OFFSET,
    0x31,
    0xF6,
    0x31,
    0xD2,
    0xCD,
    EX_USER_SYSCALL_VECTOR,
    0x3D,
    KI_U32_LE_BYTES(KI_USER_CAPS_PAYLOAD_EXPECTED_WAIT_STATUS),
    0x0F,
    0x85,
    KI_U32_LE_BYTES(36U),

    0xB9,
    KI_U32_LE_BYTES((uint32_t)EX_USER_IMAGE_CONST_BASE),
    0xB8,
    KI_U32_LE_BYTES(EX_USER_SYS_CLOSE),
    0x8B,
    0x79,
    EX_USER_CAPABILITY_WAIT_OBJECT_OFFSET,
    0x31,
    0xF6,
    0x31,
    0xD2,
    0xCD,
    EX_USER_SYSCALL_VECTOR,
    0x85,
    0xC0,
    0x0F,
    0x85,
    KI_U32_LE_BYTES(9U),

    0xB8,
    KI_U32_LE_BYTES(EX_USER_BRINGUP_SYS_RAW_EXIT),
    0x31,
    0xFF,
    0xCD,
    EX_USER_SYSCALL_VECTOR,
    0x0F,
    0x0B,
};

void
RunUserCapsDemo(void)
{
    const EX_USER_IMAGE image = {
        .Name = "user_caps",
        .NameLength = sizeof("user_caps") - 1U,
        .ProgramId = EX_PROGRAM_ID_USER_CAPS,
        .Kind = EX_USER_IMAGE_KIND_EMBEDDED_SPLIT,
        .CodeBytes = gKiUserCapsCodeBytes,
        .CodeLength = sizeof(gKiUserCapsCodeBytes),
        .ConstBytes = (const uint8_t *)gKiUserCapsConstBytes,
        .ConstLength = KI_USER_CAPS_PAYLOAD_MESSAGE_LENGTH,
        .EntryOffset = KI_USER_CAPS_PAYLOAD_ENTRY_OFFSET,
        .DefaultStackSize = EX_USER_IMAGE_PAGE_SIZE,
        .RequestedCapabilities = 0,
    };
    uint32_t pid = 0;

    HO_STATUS status = ExSpawnProgramImage(&image, EX_USER_SPAWN_FLAG_NONE, &pid);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to spawn user_caps sentinel");
}

#undef KI_U32_LE_BYTES
#undef KI_U32_BYTE
