/**
 * HimuOperatingSystem
 *
 * File: demo/user_hello.c
 * Description: Minimal fixed-layout user_hello payload and profile wiring.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "demo_internal.h"

#define KI_U32_BYTE(value, shift) ((uint8_t)((((uint32_t)(value)) >> (shift)) & 0xFFU))
#define KI_U32_LE_BYTES(value)    KI_U32_BYTE((value), 0), KI_U32_BYTE((value), 8), KI_U32_BYTE((value), 16), \
                                 KI_U32_BYTE((value), 24)

enum
{
    KI_USER_HELLO_PAYLOAD_ENTRY_OFFSET = 0U,
    KI_USER_HELLO_PAYLOAD_HELLO_OFFSET = 0U,
};

static void KiUnexpectedUserHelloKernelEntry(void *arg);

static const char gKiUserHelloConstBytes[] = KE_USER_BOOTSTRAP_LOG_HELLO "\n";

enum
{
    KI_USER_HELLO_PAYLOAD_HELLO_LENGTH = sizeof(gKiUserHelloConstBytes) - 1U,
};

static const uint8_t gKiUserHelloCodeBytes[] = {
    0xB8, KI_U32_LE_BYTES(SYS_RAW_WRITE),
    0xBF, KI_U32_LE_BYTES((uint32_t)(KE_USER_BOOTSTRAP_CONST_BASE + KI_USER_HELLO_PAYLOAD_HELLO_OFFSET)),
    0xBE, KI_U32_LE_BYTES(KI_USER_HELLO_PAYLOAD_HELLO_LENGTH),
    0x31, 0xD2,
    0xCD, KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
    0xB8, KI_U32_LE_BYTES(SYS_RAW_EXIT),
    0x31, 0xFF,
    0xCD, KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
    0x0F, 0x0B,
};

static void
KiUnexpectedUserHelloKernelEntry(void *arg)
{
    (void)arg;
    HO_KPANIC(EC_INVALID_STATE, "user_hello bootstrap thread unexpectedly executed the kernel entry point");
}

void
RunUserHelloDemo(void)
{
    KE_USER_BOOTSTRAP_CREATE_PARAMS createParams = {
        .CodeBytes = gKiUserHelloCodeBytes,
        .CodeLength = sizeof(gKiUserHelloCodeBytes),
        .EntryOffset = KI_USER_HELLO_PAYLOAD_ENTRY_OFFSET,
        .ConstBytes = gKiUserHelloConstBytes,
        .ConstLength = KI_USER_HELLO_PAYLOAD_HELLO_LENGTH,
    };
    KE_USER_BOOTSTRAP_STAGING *staging = NULL;
    KTHREAD *thread = NULL;

    HO_STATUS status = KeUserBootstrapCreateStaging(&createParams, &staging);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create staged user_hello payload");

    status = KeThreadCreate(&thread, KiUnexpectedUserHelloKernelEntry, NULL);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_hello bootstrap thread");

    thread->Flags |= KTHREAD_FLAG_BOOTSTRAP_USER;

    status = KeUserBootstrapAttachThread(thread, staging);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to attach staged user_hello payload to bootstrap thread");

    status = KeThreadStart(thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_hello bootstrap thread");
}

#undef KI_U32_LE_BYTES
#undef KI_U32_BYTE