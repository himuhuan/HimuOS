/**
 * HimuOperatingSystem
 *
 * File: demo_shell.h
 * Description: Narrow demo-shell P2 runtime helpers shared with the bootstrap
 *              syscall layer.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

HO_KERNEL_API void KeDemoShellResetControlPlane(void);
HO_KERNEL_API HO_STATUS KeDemoShellSpawnProgram(const char *programName,
                                                uint32_t programNameLength,
                                                uint32_t flags,
                                                uint32_t *outPid);
HO_KERNEL_API HO_STATUS KeDemoShellWaitPid(uint32_t pid);
HO_KERNEL_API HO_STATUS KeDemoShellKillPid(uint32_t pid);
HO_KERNEL_API BOOL KeDemoShellShouldTerminateCurrentThread(uint32_t *outProgramId);
