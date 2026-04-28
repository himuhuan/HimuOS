/**
 * HimuOperatingSystem
 *
 * File: ex/ex_bootstrap.c
 * Description: Ex-owned bootstrap runtime init facade.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ex/ex_bootstrap.h>

#include <kernel/ex/ex_bootstrap_adapter.h>
#include <kernel/ke/user_bootstrap.h>

HO_STATUS
ExBootstrapInit(void)
{
    HO_STATUS status = KeUserBootstrapRawSyscallInit();
    if (status != EC_SUCCESS)
        return status;

    return ExBootstrapAdapterInit();
}
