/**
 * HimuOperatingSystem
 *
 * File: ex/user_syscall_abi.h
 * Description: Stable Ex-facing userspace syscall ABI.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

/*
 * Stable userspace syscall trap ABI:
 * - Entry is a synchronous int 0x80 trap.
 * - RAX carries the syscall number on entry and the return value on exit.
 * - RDI, RSI, and RDX carry the first three syscall arguments.
 * - Failures return negative HO_STATUS values.
 */
#define EX_USER_SYSCALL_VECTOR           0x80U
#define EX_USER_SYSCALL_WRITE_MAX_LENGTH 256U
#define EX_USER_READLINE_MAX_LENGTH      128U

#define EX_USER_SYSCALL_BASE 0x100U

#define EX_USER_SYS_WRITE         (EX_USER_SYSCALL_BASE + 0U)
#define EX_USER_SYS_CLOSE         (EX_USER_SYSCALL_BASE + 1U)
#define EX_USER_SYS_WAIT_ONE      (EX_USER_SYSCALL_BASE + 2U)
#define EX_USER_SYS_EXIT          (EX_USER_SYSCALL_BASE + 3U)
#define EX_USER_SYS_READLINE      (EX_USER_SYSCALL_BASE + 4U)
#define EX_USER_SYS_SPAWN_PROGRAM (EX_USER_SYSCALL_BASE + 5U)
#define EX_USER_SYS_WAIT_PID      (EX_USER_SYSCALL_BASE + 6U)
#define EX_USER_SYS_SLEEP_MS      (EX_USER_SYSCALL_BASE + 7U)
#define EX_USER_SYS_KILL_PID      (EX_USER_SYSCALL_BASE + 8U)
#define EX_USER_SYS_QUERY_SYSINFO (EX_USER_SYSCALL_BASE + 9U)

#define EX_USER_WAIT_ONE_TIMEOUT_MAX_MS    0xFFFFFFFFULL
#define EX_USER_WAIT_ONE_TIMEOUT_NS_PER_MS 1000000ULL
#define EX_USER_SLEEP_MS_MAX               0xFFFFFFFFULL
#define EX_USER_SLEEP_NS_PER_MS            1000000ULL

#define EX_USER_SPAWN_FLAG_NONE       0U
#define EX_USER_SPAWN_FLAG_FOREGROUND 0x00000001U
