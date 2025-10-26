/**
 * HimuOperatingSystem
 *
 * File: stddef.h
 * Description: This header is part of types support library, in particular, it provides additional basic types and
 * convenience macros. Created: 2025-07-25 00:29:06 LiuHuan
 * @Last Modified by: LiuHuan
 * @Last Modified time: 2025-07-26 18:10:34
 * Copyright(c) 2024-2025 HimuOperatingSystem, all rights reserved.
 */

#pragma once

#ifndef LIB_STDDEF_H
#define LIB_STDDEF_H

#include "_stdbase.h"

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#ifdef __x86_64__
typedef unsigned long long size_t;
typedef long long ssize_t;
#else
typedef unsigned long size_t;
#endif
#endif

#endif // LIB_STDDEF_H
