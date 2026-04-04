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

typedef struct KE_PROCESS_ADDRESS_SPACE
{
    HO_PHYSICAL_ADDRESS RootPageTablePhys;
    BOOL Initialized;
} KE_PROCESS_ADDRESS_SPACE;

typedef struct KE_PT_MAPPING
{
    BOOL Present;
    BOOL LargeLeaf;
    BOOL UserAccessible; // Every present entry in the resolved translation path carries PTE_USER.
    uint8_t Level; // 1 = 4KB PT leaf, 2 = 2MB PD leaf, 3 = 1GB PDPT leaf
    uint64_t PageSize;
    HO_PHYSICAL_ADDRESS PhysicalBase;
    uint64_t Attributes;
} KE_PT_MAPPING;

typedef enum KE_KVA_ARENA_TYPE
{
    KE_KVA_ARENA_STACK = 0,
    KE_KVA_ARENA_FIXMAP,
    KE_KVA_ARENA_HEAP,
    KE_KVA_ARENA_MAX,
} KE_KVA_ARENA_TYPE;

typedef struct KE_KVA_RANGE
{
    KE_KVA_ARENA_TYPE Arena;
    uint32_t RecordId;
    uint64_t Generation;
    HO_VIRTUAL_ADDRESS BaseAddress;
    HO_VIRTUAL_ADDRESS UsableBase;
    uint64_t TotalPages;
    uint64_t UsablePages;
    uint64_t GuardLowerPages;
    uint64_t GuardUpperPages;
} KE_KVA_RANGE;

typedef struct KE_KVA_ARENA_INFO
{
    KE_KVA_ARENA_TYPE Arena;
    HO_VIRTUAL_ADDRESS BaseAddress;
    HO_VIRTUAL_ADDRESS EndAddressExclusive;
    uint64_t TotalPages;
    uint64_t FreePages;
    uint64_t ActiveAllocations;
    BOOL OverlapsImportedRegions;
} KE_KVA_ARENA_INFO;

typedef struct KE_KVA_USAGE_INFO
{
    uint64_t ActiveRangeCount;
    uint64_t FixmapTotalSlots;
    uint64_t FixmapActiveSlots;
} KE_KVA_USAGE_INFO;

#define KE_KVA_ACTIVE_RANGE_SNAPSHOT_MAX 16U

typedef struct KE_KVA_ACTIVE_RANGE_ENTRY
{
    KE_KVA_ARENA_TYPE Arena;
    uint32_t RecordId;
    uint64_t Generation;
    HO_VIRTUAL_ADDRESS BaseAddress;
    HO_VIRTUAL_ADDRESS EndAddressExclusive;
    HO_VIRTUAL_ADDRESS UsableBase;
    HO_VIRTUAL_ADDRESS UsableEndExclusive;
    uint64_t TotalPages;
    uint64_t UsablePages;
    uint64_t GuardLowerPages;
    uint64_t GuardUpperPages;
} KE_KVA_ACTIVE_RANGE_ENTRY;

typedef struct KE_KVA_ACTIVE_RANGE_SNAPSHOT
{
    uint64_t TotalActiveRangeCount;
    uint32_t ReturnedRangeCount;
    BOOL Truncated;
    KE_KVA_ACTIVE_RANGE_ENTRY Ranges[KE_KVA_ACTIVE_RANGE_SNAPSHOT_MAX];
} KE_KVA_ACTIVE_RANGE_SNAPSHOT;

typedef struct KE_TEMP_PHYS_MAP_HANDLE
{
    uint64_t Token;
} KE_TEMP_PHYS_MAP_HANDLE;

typedef enum KE_KVA_ADDRESS_KIND
{
    KE_KVA_ADDRESS_OUTSIDE = 0,
    KE_KVA_ADDRESS_FREE_HOLE,
    KE_KVA_ADDRESS_GUARD_PAGE,
    KE_KVA_ADDRESS_ACTIVE_STACK,
    KE_KVA_ADDRESS_ACTIVE_FIXMAP,
    KE_KVA_ADDRESS_ACTIVE_HEAP,
    KE_KVA_ADDRESS_UNKNOWN,
} KE_KVA_ADDRESS_KIND;

typedef struct KE_KVA_ADDRESS_INFO
{
    KE_KVA_ADDRESS_KIND Kind;
    BOOL InKvaArena;
    KE_KVA_ARENA_TYPE Arena;
    HO_VIRTUAL_ADDRESS ArenaBase;
    HO_VIRTUAL_ADDRESS ArenaEndExclusive;
    uint64_t ArenaPageIndex;
    BOOL HasRange;
    KE_KVA_RANGE Range;
} KE_KVA_ADDRESS_INFO;

typedef enum KE_ALLOCATOR_ALLOCATION_KIND
{
    KE_ALLOCATOR_ALLOCATION_UNKNOWN = 0,
    KE_ALLOCATOR_ALLOCATION_SMALL,
    KE_ALLOCATOR_ALLOCATION_LARGE,
} KE_ALLOCATOR_ALLOCATION_KIND;

typedef struct KE_ALLOCATOR_ADDRESS_INFO
{
    BOOL LiveAllocation;
    KE_ALLOCATOR_ALLOCATION_KIND Kind;
    HO_VIRTUAL_ADDRESS AllocationBase;
    HO_VIRTUAL_ADDRESS AllocationEndExclusive;
    uint64_t RequestedSize;
    HO_VIRTUAL_ADDRESS BackingUsableBase;
    uint64_t BackingUsablePages;
    uint32_t SmallClassIndex;
    uint32_t SmallClassSize;
} KE_ALLOCATOR_ADDRESS_INFO;

typedef struct KE_VA_DIAGNOSIS
{
    HO_VIRTUAL_ADDRESS VirtualAddress;
    HO_STATUS ImportedStatus;
    const KE_IMPORTED_REGION *ImportedRegion;
    BOOL ImportedRegionMatched;
    HO_STATUS PtStatus;
    KE_PT_MAPPING PtMapping;
    HO_STATUS KvaStatus;
    KE_KVA_ADDRESS_INFO KvaInfo;
    HO_STATUS AllocatorStatus;
    KE_ALLOCATOR_ADDRESS_INFO AllocatorInfo;
} KE_VA_DIAGNOSIS;

typedef struct KE_ALLOCATOR_STATS
{
    uint64_t LiveAllocationCount;
    uint64_t LiveSmallAllocationCount;
    uint64_t LiveLargeAllocationCount;
    uint64_t BackingBytes;
    uint64_t FailedAllocationCount;
} KE_ALLOCATOR_STATS;

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
 * Create or destroy a process-private root page-table handle.
 *
 * KeGetKernelAddressSpace() remains the public accessor for the imported kernel root. These APIs define the minimal
 * mechanism boundary for distinct process-private roots without changing existing imported-root callers. The create
 * entry treats outSpace as pure output storage and does not inspect its incoming contents.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeCreateProcessAddressSpace(KE_PROCESS_ADDRESS_SPACE *outSpace);

HO_KERNEL_API HO_STATUS KeDestroyProcessAddressSpace(KE_PROCESS_ADDRESS_SPACE *space);

/**
 * Query or switch the active root page table by physical address.
 *
 * This is a strategy-free mechanism surface for process-private roots. KeSwitchAddressSpace() does not take
 * EX_PROCESS* or KTHREAD* and leaves ownership and scheduling policy to higher layers.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeQueryActiveRootPageTable(HO_PHYSICAL_ADDRESS *outRootPageTablePhys);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeSwitchAddressSpace(HO_PHYSICAL_ADDRESS rootPageTablePhys);

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
 * On success, @outMapping->Present reports whether the page is currently mapped. @outMapping->UserAccessible reports
 * whether the full resolved translation path, including the leaf, currently carries PTE_USER and is therefore
 * reachable from Ring3. If a larger 2MB or 1GB imported leaf covers the queried page, the query reports that large
 * leaf without splitting it and returns the resolved backing physical 4KB page in PhysicalBase.
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
 * Run boot-time imported-root and private-root PT self-tests.
 *
 * This verifies imported-root query/map/protect/unmap behavior in a safe high-half hole and exercises private-root
 * create/switch/destroy coverage while keeping HHDM as the bootstrap escape hatch. The self-test performs local TLB
 * maintenance only and leaves no persistent scratch mapping or private root behind.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KePtSelfTest(void);

/**
 * Diagnose an arbitrary kernel virtual address by composing imported-region, PT, and KVA state.
 *
 * The routine is best-effort and keeps per-layer status fields in KE_VA_DIAGNOSIS so callers can still print useful
 * context if PT or KVA initialization has not happened yet.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeDiagnoseVirtualAddress(const KE_KERNEL_ADDRESS_SPACE *space,
                                                              HO_VIRTUAL_ADDRESS virtAddr,
                                                              KE_VA_DIAGNOSIS *outDiagnosis);

/**
 * Initialize the kernel virtual address allocator and its arena records.
 *
 * Callers that depend on heap-backed page allocation (for example KePool via
 * KeHeapAllocPages()) must run only after this initialization succeeds.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaInit(void);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaAllocRange(KE_KVA_ARENA_TYPE arena,
                                                     uint64_t usablePages,
                                                     uint64_t guardLowerPages,
                                                     uint64_t guardUpperPages,
                                                     BOOL ownsPhysicalBacking,
                                                     KE_KVA_RANGE *outRange);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaMapPage(const KE_KVA_RANGE *range,
                                                  uint64_t usablePageIndex,
                                                  HO_PHYSICAL_ADDRESS physAddr,
                                                  uint64_t attributes);

/**
 * Map freshly allocated physical backing for a KVA-owned range.
 *
 * On failure, this routine rolls back any partial mappings and releases the range before returning the error.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaMapOwnedPages(const KE_KVA_RANGE *range, uint64_t attributes);

/**
 * Release the exact live range described by a previously returned handle.
 *
 * This validates the slot, generation, and layout fields before teardown so a
 * stale `KE_KVA_RANGE` cannot release a recycled allocation.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaReleaseRangeHandle(const KE_KVA_RANGE *range);

/**
 * Release the current range that owns @usableBase.
 *
 * This is an address-based convenience API. Callers that need recycle-safe
 * ownership semantics should retain the original `KE_KVA_RANGE` handle and use
 * `KeKvaReleaseRangeHandle()`.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaReleaseRange(HO_VIRTUAL_ADDRESS usableBase);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaQueryRange(HO_VIRTUAL_ADDRESS usableBase, KE_KVA_RANGE *outRange);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaQueryArenaInfo(KE_KVA_ARENA_TYPE arena, KE_KVA_ARENA_INFO *outInfo);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaQueryUsageInfo(KE_KVA_USAGE_INFO *outInfo);

/**
 * Copy a bounded, internally consistent snapshot of active KVA ranges.
 *
 * The allocator serializes range-table publication and recycle while this
 * snapshot is assembled, so returned entries never expose partially
 * initialized or already-recycled records.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaQueryActiveRanges(KE_KVA_ACTIVE_RANGE_SNAPSHOT *outSnapshot);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaClassifyAddress(HO_VIRTUAL_ADDRESS virtAddr, KE_KVA_ADDRESS_INFO *outInfo);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaValidateLayout(void);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeKvaSelfTest(void);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeFixmapAcquire(HO_PHYSICAL_ADDRESS physAddr,
                                                     uint64_t attributes,
                                                     KE_TEMP_PHYS_MAP_HANDLE *outHandle,
                                                     HO_VIRTUAL_ADDRESS *outVirtAddr);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeFixmapRelease(KE_TEMP_PHYS_MAP_HANDLE *handle);

/**
 * Acquire a short-lived runtime alias for one physical page through fixmap.
 *
 * This wrapper keeps runtime callers from adopting direct HHDM aliases as
 * ownership addresses and returns an opaque release handle that encodes the
 * current fixmap-slot ownership token instead of exposing slot identifiers
 * directly.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeTempPhysMapAcquire(HO_PHYSICAL_ADDRESS physAddr,
                                                          uint64_t attributes,
                                                          KE_TEMP_PHYS_MAP_HANDLE *outHandle,
                                                          HO_VIRTUAL_ADDRESS *outVirtAddr);

HO_KERNEL_API HO_NODISCARD HO_STATUS KeTempPhysMapRelease(KE_TEMP_PHYS_MAP_HANDLE *handle);

/**
 * Allocate heap-backed kernel virtual pages from the KVA heap arena.
 *
 * This reserves a KVA range in the heap arena and maps owned physical pages
 * into it. The caller receives the usable virtual base address.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeHeapAllocPages(uint64_t pageCount, HO_VIRTUAL_ADDRESS *outVirtAddr);

/**
 * Release a previous KeHeapAllocPages() allocation by its usable base address.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeHeapFreePages(HO_VIRTUAL_ADDRESS baseVirt);

/**
 * Initialize the kernel allocator layer on top of the heap foundation.
 *
 * Phase-one bootstrap only publishes allocator initialization state and a
 * stable accounting query surface. Allocation internals are introduced in a
 * later phase.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeAllocatorInit(void);

/**
 * Query allocator accounting snapshot.
 *
 * Returns EC_INVALID_STATE before KeAllocatorInit() has succeeded.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeAllocatorQueryStats(KE_ALLOCATOR_STATS *outStats);

/**
 * Diagnose allocator-owned meaning for a virtual address.
 *
 * The caller should use this as an appended interpretation layer after KVA
 * classification has already established that the address is in active heap
 * backing.
 */
HO_KERNEL_API HO_NODISCARD HO_STATUS KeAllocatorDiagnoseAddress(HO_VIRTUAL_ADDRESS virtAddr,
                                                                KE_ALLOCATOR_ADDRESS_INFO *outInfo);

/**
 * Allocate a kernel-owned object buffer from allocator-managed storage.
 */
HO_KERNEL_API void *kmalloc(size_t size);

/**
 * Allocate a zeroed kernel-owned object buffer from allocator-managed storage.
 */
HO_KERNEL_API void *kzalloc(size_t size);

/**
 * Free a previous allocator allocation.
 *
 * Passing NULL is always a no-op.
 */
HO_KERNEL_API void kfree(void *ptr);
