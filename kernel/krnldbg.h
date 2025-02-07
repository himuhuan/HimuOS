/**
 * HIMU OPERATING SYSTEM
 *
 * File: krnldbg.h
 * Kernel Debug Utilities
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS__KERNEL_KRNLDBG_H
#define __HIMUOS__KERNEL_KRNLDBG_H

void KPanic(const char *fileName, int line, const char *func, const char *msg);

#define KPANIC(...) KPanic(__FILE__, __LINE__, __func__, __VA_ARGS__)

#ifndef NDEBUG
#define KASSERT(cond)                                                                                                  \
    if (!(cond)) {                                                                                                     \
        KPanic(__FILE__, __LINE__, __func__, "ASSERTION FAILED: " #cond);                                              \
    }

#include "krnlio.h"

#else
#define KASSERT(cond) ((void)0)
#endif /* ^^ NDEBUG ^^ */

#endif /* ^^ __HIMUOS__KERNEL_KRNLDBG_H  ^^ */