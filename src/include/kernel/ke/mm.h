#pragma once

#include <arch/amd64/pm.h>
#include <kernel/hodefs.h>

#define HHDM_PHYS2VIRT(addr) ((HO_VIRTUAL_ADDRESS)((HO_PHYSICAL_ADDRESS)(addr) + HHDM_BASE_VA))

// Forward declarations
struct BOOT_CAPSULE;

// PMM public types
typedef struct KE_PMM_ALLOC_CONSTRAINTS
{
    HO_PHYSICAL_ADDRESS MaxPhysAddr; // 0 = no limit
    uint64_t AlignmentPages;         // 0 or 1 = no alignment requirement; must be power of 2
} KE_PMM_ALLOC_CONSTRAINTS;

typedef struct KE_PMM_STATS
{
    uint64_t TotalBytes;
    uint64_t FreeBytes;
    uint64_t AllocatedBytes;
    uint64_t ReservedBytes;
} KE_PMM_STATS;

HO_KERNEL_API HO_NODISCARD HO_STATUS KePmmInitFromBootMemoryMap(struct BOOT_CAPSULE *capsule);

HO_KERNEL_API HO_NODISCARD HO_STATUS KePmmAllocPages(uint64_t count,
                                                     const KE_PMM_ALLOC_CONSTRAINTS *constraints,
                                                     HO_PHYSICAL_ADDRESS *outBasePhys);

HO_KERNEL_API HO_STATUS KePmmFreePages(HO_PHYSICAL_ADDRESS basePhys, uint64_t count);

HO_KERNEL_API HO_STATUS KePmmReservePages(HO_PHYSICAL_ADDRESS basePhys, uint64_t count);

HO_KERNEL_API HO_STATUS KePmmQueryStats(KE_PMM_STATS *outStats);

HO_KERNEL_API HO_STATUS KePmmCheckInvariants(void);