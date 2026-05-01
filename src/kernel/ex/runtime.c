/**
 * HimuOperatingSystem
 *
 * File: ex/runtime.c
 * Description: Ex-owned runtime runtime init facade.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ex/ex_runtime.h>

#include <kernel/ex/ex_user_runtime.h>
#include <kernel/ex/program.h>
#include <kernel/ke/user_mode.h>

HO_STATUS
ExRuntimeInit(void)
{
    HO_STATUS status = ExProgramValidateBuiltins();
    if (status != EC_SUCCESS)
        return status;

    status = KeUserModeRawSyscallInit();
    if (status != EC_SUCCESS)
        return status;

    return ExUserRuntimeInit();
}
