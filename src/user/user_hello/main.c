/**
 * HimuOperatingSystem
 *
 * File: user/user_hello/main.c
 * Description: Bootstrap-only C userspace hello payload matching the current
 *              staged raw-write/raw-exit evidence chain.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys_bringup.h"

static const char gKiUserHelloMessage[] = EX_USER_REGRESSION_LOG_HELLO "\n";

enum
{
    KI_USER_HELLO_MESSAGE_LENGTH = sizeof(gKiUserHelloMessage) - 1U,
};

int
main(void)
{
    int64_t status = 0;

    HoUserWaitForP1Gate();

    status = HoUserRawProbeGuardPageByte();
    if (status != -(int64_t)EC_ILLEGAL_ARGUMENT)
        HoUserAbort();

    status = HoUserRawWrite(gKiUserHelloMessage, KI_USER_HELLO_MESSAGE_LENGTH);
    if (status != (int64_t)KI_USER_HELLO_MESSAGE_LENGTH)
        HoUserAbort();

    HoUserRawExit(0);
}
