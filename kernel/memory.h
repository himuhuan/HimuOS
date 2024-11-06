/**
 * HIMU OPERATING SYSTEM
 *
 * File: memory.h
 * Memory management & pool
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS_KERNEL_MEMORY_H
#define __HIMUOS_KERNEL_MEMORY_H

#include <stddef.h>
#include "structs/bitmap.h"

struct KR_VIRTUAL_ADDRESS {
    struct KR_BITMAP VrAddrBitmap;
    uint32_t         VrAddrStart;
};

struct KR_MEMORY_POOL {
    struct KR_BITMAP PoolBitmap;
    uint32_t         PhyAddrStart;
    uint32_t         PoolSize;
};

enum KR_MEMORY_POOL_TYPE { MEMORY_POOL_KERNEL = 1, MEMORY_POOL_USER = 2 };

extern struct KR_MEMORY_POOL gKernelPool, gUserPool;

void KrnlMemInit(void);

#define MEM_PAGE_SIZE       4096
#define MEM_PAGE_P_0        0
#define MEM_PAGE_P_1        1
#define MEM_PAGE_RW_R       0
#define MEM_PAGE_RW_W       2
#define MEM_PAGE_US_S       0
#define MEM_PAGE_US_U       4

/* The top of the kernel stack is located at 0xC0400000, and a natural page needs to be reserved for PCB.
4GiB memory, requires 32 natural page size bitmaps to complete mapping */
#define MEM_BITMAP_BASE     0xC03DF000

/* The starting address of the kernel heap is located at PHY 0x500000, which is actually PDE&kernel PTE
In actual use, PDE&PTE are not visible to virtual addresses  */
#define MEM_KRNL_HEAP_START 0xC0500000

void *KrAllocMemPage(enum KR_MEMORY_POOL_TYPE type, uint32_t pageCnt);

void *KrAllocKernelMemPage(uint32_t pageCnt);

#endif // ^^ __HIMUOS_KERNEL_MEMORY_H ^^