#pragma once

#include <arch/amd64/pm.h>
#include <boot/boot_mapping_manifest.h>
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

typedef struct KE_IMPORTED_REGION
{
    HO_VIRTUAL_ADDRESS VirtualStart;
    HO_VIRTUAL_ADDRESS VirtualEndExclusive;
    HO_PHYSICAL_ADDRESS PhysicalStart;
    HO_PHYSICAL_ADDRESS PhysicalEndExclusive;
    uint64_t Size;
    uint64_t Attributes;
    uint32_t ProvenanceValue;
    uint16_t Type;
    uint8_t Provenance;
    uint8_t Lifetime;
    uint8_t Granularity;
    BOOL BootOwned;
    BOOL Pinned;
} KE_IMPORTED_REGION;

typedef struct KE_KERNEL_ADDRESS_SPACE
{
    HO_PHYSICAL_ADDRESS RootPageTablePhys;
    PAGE_TABLE_INFO ImportedRootInfo;
    KE_IMPORTED_REGION *Regions;
    HO_PHYSICAL_ADDRESS RegionArrayPhys;
    uint64_t RegionArrayBytes;
    uint32_t RegionCount;
    uint32_t BootOwnedRegionCount;
    uint32_t PinnedRegionCount;
    BOOL Initialized;
} KE_KERNEL_ADDRESS_SPACE;

typedef struct KE_PT_MAPPING
{
    BOOL Present;
    BOOL LargeLeaf;
    uint8_t Level; // 1 = 4KB PT leaf, 2 = 2MB PD leaf, 3 = 1GB PDPT leaf
    uint64_t PageSize;
    HO_PHYSICAL_ADDRESS PhysicalBase;
    uint64_t Attributes;
} KE_PT_MAPPING;

HO_KERNEL_API HO_NODISCARD HO_STATUS KePmmInitFromBootMemoryMap(struct BOOT_CAPSULE *capsule);

HO_KERNEL_API HO_NODISCARD HO_STATUS KePmmAllocPages(uint64_t count,
                                                     const KE_PMM_ALLOC_CONSTRAINTS *constraints,
                                                     HO_PHYSICAL_ADDRESS *outBasePhys);

HO_KERNEL_API HO_STATUS KePmmFreePages(HO_PHYSICAL_ADDRESS basePhys, uint64_t count);

HO_KERNEL_API HO_STATUS KePmmReservePages(HO_PHYSICAL_ADDRESS basePhys, uint64_t count);

HO_KERNEL_API HO_STATUS KePmmQueryStats(KE_PMM_STATS *outStats);

HO_KERNEL_API HO_STATUS KePmmCheckInvariants(void);

/**
 * Import the running boot-installed root page table into a first-class kernel address-space object.
 *
 * The imported object adopts the currently active root page table described by the boot capsule and seeds its region
 * catalog from the validated Boot Mapping Manifest. Phase one keeps all imported regions pinned and does not rebuild
 * mappings or switch away from the live root.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeImportKernelAddressSpace(struct BOOT_CAPSULE *capsule,
                                                                const BOOT_MAPPING_MANIFEST_HEADER *manifest);

/**
 * Return the imported kernel address-space handle after a successful import.
 *
 * Later VMM layers should use this accessor rather than reaching back into boot handoff structures directly. The
 * returned object describes the live imported root, imported manifest regions, and phase-one pinned-region metadata.
 */
HO_KERNEL_API const KE_KERNEL_ADDRESS_SPACE *KeGetKernelAddressSpace(void);

/**
 * Find the most specific imported region that covers a virtual address.
 *
 * The imported region list remains manifest-derived and may contain semantic overlays inside broader windows such as
 * HHDM. This lookup returns the smallest matching region so boot-owned overlays stay distinct from wider mappings.
 */
HO_KERNEL_API const KE_IMPORTED_REGION *KeFindImportedRegion(const KE_KERNEL_ADDRESS_SPACE *space,
                                                             HO_VIRTUAL_ADDRESS virtAddr);

/**
 * Query the imported root page table for a single 4KB virtual page.
 *
 * On success, @outMapping->Present reports whether the page is currently mapped. If a larger 2MB or 1GB imported leaf
 * covers the queried page, the query reports that large leaf without splitting it and returns the resolved backing
 * physical 4KB page in PhysicalBase.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KePtQueryPage(const KE_KERNEL_ADDRESS_SPACE *space,
                                                   HO_VIRTUAL_ADDRESS virtAddr,
                                                   KE_PT_MAPPING *outMapping);

/**
 * Install a present 4KB leaf into an unmapped hole in the imported root page table.
 *
 * Missing intermediate tables are allocated from PMM and accessed through HHDM. Phase one only supports eager 4KB
 * mappings and rejects requests that would require splitting an existing imported large leaf.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KePtMapPage(const KE_KERNEL_ADDRESS_SPACE *space,
                                                 HO_VIRTUAL_ADDRESS virtAddr,
                                                 HO_PHYSICAL_ADDRESS physAddr,
                                                 uint64_t attributes);

/**
 * Remove an existing 4KB leaf from the imported root page table.
 *
 * Phase one does not reclaim empty intermediate tables and rejects requests that would require splitting a 2MB or 1GB
 * imported leaf.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KePtUnmapPage(const KE_KERNEL_ADDRESS_SPACE *space, HO_VIRTUAL_ADDRESS virtAddr);

/**
 * Update the protection bits of an existing 4KB leaf while preserving its translation.
 *
 * The PT HAL only supports protection updates for 4KB leaves in this phase. Requests that would require large-leaf
 * splitting are rejected with EC_NOT_SUPPORTED.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KePtProtectPage(const KE_KERNEL_ADDRESS_SPACE *space,
                                                     HO_VIRTUAL_ADDRESS virtAddr,
                                                     uint64_t attributes);

/**
 * Run a boot-time PT HAL scratch mapping self-test in a safe high-half hole.
 *
 * This verifies query/map/protect/unmap behavior against the imported root while keeping HHDM as the bootstrap escape
 * hatch. The self-test performs local TLB maintenance only and leaves no persistent scratch mapping behind.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KePtSelfTest(void);
