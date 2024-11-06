/**
 * HIMU OPERATING SYSTEM
 *
 * File: memory.c
 * Memory management & pool
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */
#include "memory.h"
#include "bits/krnldefs.h"
#include "lib/kernel/krnlio.h"
#include "krnldbg.h"
#include <string.h>

#define VR_ADDR_HIGH_PART(addr) ((addr & 0xFFC00000) >> 22)
#define VR_ADDR_MID_PART(addr)  ((addr & 0x003FF000) >> 12)

struct KR_MEMORY_POOL gKernelPool, gUserPool;

struct KR_VIRTUAL_ADDRESS gKernelVrAddr;

static void MemPoolInit(uint32_t totalMem) {
    /* 1 PDE + 1 PTE (0 PTE) + 254 PTE (769 - 1022) = 256 PAGE */
    uint32_t pageTableSize = MEM_PAGE_SIZE * 256;
    /* NOTE: HimuOS reserves space of 0x100000 from the top of the kernel stack to the PDE and PTE regions*/
    uint32_t usedMem         = pageTableSize + KRNL_PHY_STACK_TOP + 0x100000;
    uint32_t freeMem         = totalMem - usedMem;
    uint16_t allFreePages    = freeMem / MEM_PAGE_SIZE;
    uint16_t krnlFreePages   = allFreePages / 2;
    uint16_t userFreePages   = allFreePages - krnlFreePages;
    uint32_t krnlPoolBtmpLen = krnlFreePages / 8;
    uint32_t userPoolBtmpLen = userFreePages / 8;

    /* kernel pool */
    gKernelPool.PhyAddrStart = usedMem;
    gKernelPool.PoolSize     = krnlFreePages * MEM_PAGE_SIZE;
    KrBitMapInit(&gKernelPool.PoolBitmap, (BYTE *)MEM_BITMAP_BASE, krnlPoolBtmpLen);

    /* user pool */
    gUserPool.PhyAddrStart = gKernelPool.PhyAddrStart + krnlFreePages * MEM_PAGE_SIZE;
    gUserPool.PoolSize     = userFreePages * MEM_PAGE_SIZE;
    KrBitMapInit(&gUserPool.PoolBitmap, (BYTE *)MEM_BITMAP_BASE + krnlPoolBtmpLen, userPoolBtmpLen);

    /* kernel heap virtual address */
    gKernelVrAddr.VrAddrStart = MEM_KRNL_HEAP_START;
    KrBitMapInit(&gKernelVrAddr.VrAddrBitmap, (BYTE *)MEM_BITMAP_BASE + krnlPoolBtmpLen + userPoolBtmpLen,
                 krnlPoolBtmpLen);
    // clang-format off
#if _KDBG
    PrintStr("Memory Summary\n");
    PrintStr("  Available/Total: "); PrintInt(freeMem); PrintChar('/'); PrintInt(totalMem); PrintStr(" BYTES\n");
    PrintStr("  Kernel Pool: 0x"); PrintHex(gKernelPool.PhyAddrStart); PrintStr(" -> 0x"); 
    PrintHex(gKernelPool.PhyAddrStart + gKernelPool.PoolSize);
    PrintStr(" ("); PrintInt(gKernelPool.PoolSize); PrintStr(" bytes)\n");
    PrintStr("  User Pool: 0x"); PrintHex(gUserPool.PhyAddrStart); PrintStr(" -> 0x"); 
    PrintHex(gUserPool.PhyAddrStart + gUserPool.PoolSize);
    PrintStr(" ("); PrintInt(gUserPool.PoolSize); PrintStr(" bytes)\n");
#endif
    // clang-format on
}

void KrnlMemInit() {
    PrintStr("START KrnlMemInit()\n");
    uint32_t memoryTotalBytes = (*(uint32_t *)0xb10);
    MemPoolInit(memoryTotalBytes);
    PrintStr("DONE KrnlMemInit()\n");
}

static void *GetUnallocatedVrAddr(enum KR_MEMORY_POOL_TYPE type, uint32_t pageCnt) {
    int32_t vrAddrStart = 0, bitIdx = -1;
    if (type == MEMORY_POOL_KERNEL) {
        bitIdx = KrBitMapFindClearBits(&gKernelVrAddr.VrAddrBitmap, pageCnt);
        if (bitIdx == -1)
            return NULL;
        KrBitMapSetBits(&gKernelVrAddr.VrAddrBitmap, bitIdx, pageCnt, 1);
        vrAddrStart = gKernelVrAddr.VrAddrStart + bitIdx * MEM_PAGE_SIZE;
    }
    return (void *)vrAddrStart;
}

static uint32_t *GetVrAddrPte(uint32_t vrAddr) {
    return (uint32_t *)(0xFFC00000 + ((vrAddr & 0xFFC00000) >> 10) + (VR_ADDR_MID_PART(vrAddr) << 2));
}

static uint32_t *GetVrAddrPde(uint32_t vrAddr) { return (uint32_t *)(0xFFFFF000 + (VR_ADDR_HIGH_PART(vrAddr) << 2)); }

// debug
// static void PrintMessageWithAddress(const char *message, void *addr) {
//     PrintStr(message);
//     PrintStr(": ");
//     PrintStr("0x");
//     PrintHex((uint32_t)addr);
//     PrintStr("\n");
// }

static void *AllocOnePhyPage(struct KR_MEMORY_POOL *pool) {
    int idx = KrBitMapFindClearBits(&pool->PoolBitmap, 1);
    if (idx == -1)
        return NULL;
    KrBitMapSet(&pool->PoolBitmap, idx, 1);
    return (void *)(idx * MEM_PAGE_SIZE + pool->PhyAddrStart);
}

/* Establishing a mapping between virtual addresses and physical addresses through PDE and PTE */
static void AddPageTableMap(void *virAddrPtr, void *phyPageAddrPtr) {
    uint32_t  vrAddr = (uint32_t)virAddrPtr, phyPageAddr = (uint32_t)phyPageAddrPtr;
    uint32_t *pde = GetVrAddrPde(vrAddr);
    uint32_t *pte = GetVrAddrPte(vrAddr);

    if (*pde & MEM_PAGE_P_1) { /* PDE exists */
        if (*pte & MEM_PAGE_P_1)
            KPANIC("Double allocation of PTE addresses that have already been allocated");
        *pte = (phyPageAddr | MEM_PAGE_US_U | MEM_PAGE_RW_W | MEM_PAGE_P_1);
    } else { /* PDE not exists */
        uint32_t pdePhyAddr = (uint32_t)AllocOnePhyPage(&gKernelPool);
        *pde                = (pdePhyAddr | MEM_PAGE_US_U | MEM_PAGE_RW_W | MEM_PAGE_P_1);
        memset((void *)((int)pte & 0xFFFFF000), 0, MEM_PAGE_SIZE);
        KASSERT(!(*pte & MEM_PAGE_P_1));
        *pte = (phyPageAddr | MEM_PAGE_US_U | MEM_PAGE_RW_W | MEM_PAGE_P_1);
    }
}

void *KrAllocMemPage(enum KR_MEMORY_POOL_TYPE type, uint32_t pageCnt) {
    KASSERT(pageCnt > 0);
    struct KR_MEMORY_POOL *pool;
    void                  *pagePhyAddr, *vrAddrStart;
    uint32_t               vrAddr;

    vrAddrStart = GetUnallocatedVrAddr(type, pageCnt);
    if (vrAddrStart == 0)
        return NULL;
    vrAddr = (uint32_t)vrAddrStart;
    pool   = (type & MEMORY_POOL_KERNEL) ? &gKernelPool : &gUserPool;
    while (pageCnt-- > 0) {
        pagePhyAddr = AllocOnePhyPage(pool);
        if (pagePhyAddr == NULL)
            return NULL;
        AddPageTableMap((void *)vrAddr, pagePhyAddr);
        vrAddr += MEM_PAGE_SIZE;
    }
    return vrAddrStart;
}

void *KrAllocKernelMemPage(uint32_t pageCnt) {
    void *krnlPage = KrAllocMemPage(MEMORY_POOL_KERNEL, pageCnt);
    if (krnlPage != NULL)
        memset(krnlPage, 0, pageCnt * MEM_PAGE_SIZE);
    return krnlPage;
}