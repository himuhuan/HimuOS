/**
 * HimuOperatingSystem
 *
 * File: user/user_hello/main.c
 * Description: Minimal formal-ABI userspace hello payload.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "libsys.h"

#include <kernel/ex/user_regression_anchors.h>

static const char gKiUserHelloMessage[] = EX_USER_REGRESSION_LOG_HELLO "\n";

enum
{
    KI_USER_HELLO_MESSAGE_LENGTH = sizeof(gKiUserHelloMessage) - 1U,
};

int
main(void)
{
    int64_t status = 0;

    if (!HoUserCurrentCapabilitySeedBlockIsValid())
        HoUserAbort();

    status = HoUserWriteStdout(HoUserImageStackGuardBase(), 1U);
    if (status != -(int64_t)EC_ILLEGAL_ARGUMENT)
        HoUserAbort();

    status = HoUserWriteStdout(gKiUserHelloMessage, KI_USER_HELLO_MESSAGE_LENGTH);
    if (status != (int64_t)KI_USER_HELLO_MESSAGE_LENGTH)
        HoUserAbort();

    HoUserExit(0);
}
