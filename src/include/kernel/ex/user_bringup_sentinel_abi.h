/**
 * HimuOperatingSystem
 *
 * File: ex/user_bringup_sentinel_abi.h
 * Description: Raw bring-up-only userspace ABI kept for sentinel profiles.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ex/user_image_abi.h>

#define EX_USER_BRINGUP_P1_MAILBOX_OFFSET  0ULL
#define EX_USER_BRINGUP_P1_MAILBOX_ADDRESS (EX_USER_IMAGE_STACK_BASE + EX_USER_BRINGUP_P1_MAILBOX_OFFSET)
#define EX_USER_BRINGUP_P1_MAILBOX_CLOSED  0U
#define EX_USER_BRINGUP_P1_MAILBOX_GATE_OPEN 0x31504741U

#define EX_USER_BRINGUP_SYS_RAW_INVALID 0U
#define EX_USER_BRINGUP_SYS_RAW_WRITE   1U
#define EX_USER_BRINGUP_SYS_RAW_EXIT    2U
