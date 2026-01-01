/**
 * HimuOperatingSystem
 *
 * File: hodefs.h
 * Description:
 * Kernel definitions and macros.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

#define HO_STACK_SIZE 0x4000 // 16KB stack size

/* NOTE: In HimuOS, every stack always includes extra one guard page
   (no physical memory allocated) to catch stack overflows. */

#define KRNL_BASE_VA       0xFFFF800000000000ULL                        // Kernel base virtual address
#define KRNL_STACK_VA      0xFFFF808000000000ULL                        // Kernel stack BOTTOM virtual address
#define KRNL_IST1_STACK_VA (KRNL_STACK_VA + HO_STACK_SIZE + PAGE_4KB) // Kernel IST1 stack BOTTOM virtual address
#define HHDM_BASE_VA       0xFFFF900000000000ULL // HHDM (Higher Half Direct Mapping) base virtual address
#define MMIO_BASE_VA       0xFFFFC00000000000ULL // MM