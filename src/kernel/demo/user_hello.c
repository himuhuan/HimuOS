/**
 * HimuOperatingSystem
 *
 * File: demo/user_hello.c
 * Description: Minimal fixed-layout user_hello payload covering P1 gate wiring,
 *              P2 raw-syscall self-check, and P3 exit-reap teardown-before-termination
 *              evidence on the shared imported root staging model.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

/*
 * Refactor anchor: keep the user_hello clean-pass evidence chain fixed as
 * first entry, timer round-trip, rejected raw write, hello write,
 * SYS_RAW_EXIT, bootstrap teardown complete, and idle/reaper reclaim.
 * This change only permits boundary refactoring around ownership and
 * registration seams for bootstrap user support.
 * It must not change the profile's logs, ordering contract, or pass/fail behavior.
 */

#include "demo_internal.h"

#include <kernel/ex/ex_bootstrap.h>

#define KI_U32_BYTE(value, shift) ((uint8_t)((((uint32_t)(value)) >> (shift)) & 0xFFU))
#define KI_U32_LE_BYTES(value)    KI_U32_BYTE((value), 0), KI_U32_BYTE((value), 8), KI_U32_BYTE((value), 16), \
                                 KI_U32_BYTE((value), 24)

enum
{
    KI_USER_HELLO_PAYLOAD_ENTRY_OFFSET = 0U,
    KI_USER_HELLO_PAYLOAD_HELLO_OFFSET = 0U,
    KI_USER_HELLO_PAYLOAD_PROBE_LENGTH = 1U,
};

static void KiUnexpectedUserHelloKernelEntry(void *arg);

static const char gKiUserHelloConstBytes[] = KE_USER_BOOTSTRAP_LOG_HELLO "\n";

enum
{
    KI_USER_HELLO_PAYLOAD_EXPECTED_INVALID_STATUS = (uint32_t)(-(int32_t)EC_ILLEGAL_ARGUMENT),
    KI_USER_HELLO_PAYLOAD_HELLO_LENGTH = sizeof(gKiUserHelloConstBytes) - 1U,
};

static const uint8_t gKiUserHelloCodeBytes[] = {
    // The same bootstrap-only user_hello profile waits for the P1 mailbox gate, then
    // explicitly verifies one rejected raw write probe, one successful hello write, and
    // finally SYS_RAW_EXIT.  P3 evidence requires the clean path to show teardown-complete
    // before thread termination, followed by idle/reaper reclaim back to stable idle.
    0xB9, KI_U32_LE_BYTES((uint32_t)KE_USER_BOOTSTRAP_STACK_MAILBOX_ADDRESS),
    0x8B, 0x01,
    0x3D, KI_U32_LE_BYTES(KE_USER_BOOTSTRAP_P1_MAILBOX_GATE_OPEN),
    0x75, 0xF7,
    0xB8, KI_U32_LE_BYTES(SYS_RAW_WRITE),
    0xBF, KI_U32_LE_BYTES((uint32_t)KE_USER_BOOTSTRAP_STACK_GUARD_BASE),
    0xBE, KI_U32_LE_BYTES(KI_USER_HELLO_PAYLOAD_PROBE_LENGTH),
    0x31, 0xD2,
    0xCD, KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
    0x3D, KI_U32_LE_BYTES(KI_USER_HELLO_PAYLOAD_EXPECTED_INVALID_STATUS),
    0x75, 0x23,
    0xB8, KI_U32_LE_BYTES(SYS_RAW_WRITE),
    0xBF, KI_U32_LE_BYTES((uint32_t)(KE_USER_BOOTSTRAP_CONST_BASE + KI_USER_HELLO_PAYLOAD_HELLO_OFFSET)),
    0xBE, KI_U32_LE_BYTES(KI_USER_HELLO_PAYLOAD_HELLO_LENGTH),
    0x31, 0xD2,
    0xCD, KE_USER_BOOTSTRAP_SYSCALL_VECTOR,
    0x3D, KI_U32_LE_BYTES(KI_USER_HELLO_PAYLOAD_HELLO_LENGTH),
    0x75, 0x09,
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
    EX_BOOTSTRAP_PROCESS_CREATE_PARAMS createParams = {
        .CodeBytes = gKiUserHelloCodeBytes,
        .CodeLength = sizeof(gKiUserHelloCodeBytes),
        .EntryOffset = KI_USER_HELLO_PAYLOAD_ENTRY_OFFSET,
        .ConstBytes = gKiUserHelloConstBytes,
        .ConstLength = KI_USER_HELLO_PAYLOAD_HELLO_LENGTH,
    };
    EX_BOOTSTRAP_THREAD_CREATE_PARAMS threadParams = {
        .EntryPoint = KiUnexpectedUserHelloKernelEntry,
        .EntryArg = NULL,
    };
    EX_PROCESS *process = NULL;
    EX_THREAD *thread = NULL;

    HO_STATUS status = ExBootstrapCreateProcess(&createParams, &process);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create staged user_hello payload");

    status = ExBootstrapCreateThread(&process, &threadParams, &thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to create user_hello bootstrap thread");

    status = ExBootstrapStartThread(&thread);
    if (status != EC_SUCCESS)
        HO_KPANIC(status, "Failed to start user_hello bootstrap thread");
}

#undef KI_U32_LE_BYTES
#undef KI_U32_BYTE
