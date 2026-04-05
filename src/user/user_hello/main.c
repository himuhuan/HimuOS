/**
 * HimuOperatingSystem
 *
 * File: user/user_hello/main.c
 * Description: Bootstrap-only C userspace hello payload matching the current
 *              staged raw-write/raw-exit evidence chain.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

enum
{
    KI_USER_HELLO_PROBE_LENGTH = 1U,
};

static const char gKiUserHelloMessage[] = KE_USER_BOOTSTRAP_LOG_HELLO "\n";

enum
{
    KI_USER_HELLO_MESSAGE_LENGTH = sizeof(gKiUserHelloMessage) - 1U,
};

int
main(void)
{
    int64_t status = 0;

    HoUserWaitForP1Gate();

    status = HoUserRawWrite((const void *)(uint64_t)KE_USER_BOOTSTRAP_STACK_GUARD_BASE, KI_USER_HELLO_PROBE_LENGTH);
    if (status != -(int64_t)EC_ILLEGAL_ARGUMENT)
        HoUserAbort();

    status = HoUserRawWrite(gKiUserHelloMessage, KI_USER_HELLO_MESSAGE_LENGTH);
    if (status != (int64_t)KI_USER_HELLO_MESSAGE_LENGTH)
        HoUserAbort();

    HoUserRawExit(0);
}
