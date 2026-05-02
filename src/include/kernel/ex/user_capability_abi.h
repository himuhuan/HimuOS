/**
 * HimuOperatingSystem
 *
 * File: ex/user_capability_abi.h
 * Description: Stable initial userspace capability seed contract.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#ifndef __ASSEMBLER__
#include <_hobase.h>
#endif

#include <kernel/ex/user_image_abi.h>

#define EX_USER_CAPABILITY_INVALID_HANDLE      0U
#define EX_USER_CAPABILITY_SEED_VERSION        1U
#define EX_USER_CAPABILITY_SEED_VERSION_OFFSET 0U
#define EX_USER_CAPABILITY_SEED_SIZE_OFFSET    4U
#define EX_USER_CAPABILITY_PROCESS_SELF_OFFSET 8U
#define EX_USER_CAPABILITY_THREAD_SELF_OFFSET  12U
#define EX_USER_CAPABILITY_STDOUT_OFFSET       16U
#define EX_USER_CAPABILITY_WAIT_OBJECT_OFFSET  20U
#define EX_USER_CAPABILITY_SEED_BLOCK_SIZE     24U

#define EX_USER_CAPABILITY_SEED_ADDRESS   EX_USER_IMAGE_CONST_BASE
#define EX_USER_IMAGE_CONST_PAYLOAD_OFFSET EX_USER_CAPABILITY_SEED_BLOCK_SIZE

#ifndef __ASSEMBLER__
typedef struct __attribute__((packed)) EX_USER_CAPABILITY_SEED_BLOCK
{
    uint32_t Version;
    uint32_t Size;
    uint32_t ProcessSelf;
    uint32_t ThreadSelf;
    uint32_t Stdout;
    uint32_t WaitObject;
} EX_USER_CAPABILITY_SEED_BLOCK;
#endif
