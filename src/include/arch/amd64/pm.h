/**
 * HimuOperatingSystem
 *
 * File: pm.h
 * Description:
 * Protected Mode and GDT related definitions.
 *
 * Only for AMD64 architecture.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

/* GDT Entry Structure: 8bytes */
typedef struct _GDT_ENTRY
{
    uint16_t LimitLow;
    uint16_t BaseLow;
    uint8_t BaseMiddle;
    uint8_t Access;
    uint8_t Granularity;
    uint8_t BaseHigh;
} __attribute__((packed)) GDT_ENTRY;

#define NGDT                8 // the # of GDT entries
#define GDT_KRNL_CODE_INDEX 1
#define GDT_KRNL_DATA_INDEX 2
#define GDT_USER_CODE_INDEX 3
#define GDT_USER_DATA_INDEX 4
#define GDT_TSS_INDEX       5 // TSS Descriptor starts from index 5 (uses two entries)

// TSS: Task State Segment, at least 104 bytes in size
typedef struct _TSS64
{
    uint32_t Reserved0;
    uint64_t RSP0; // Stack pointer for privilege level 0
    uint64_t RSP1; // Stack pointer for privilege level 1
    uint64_t RSP2; // Stack pointer for privilege level 2
    uint64_t Reserved1;
    uint64_t IST1;
    uint64_t IST2;
    uint64_t IST3;
    uint64_t IST4;
    uint64_t IST5;
    uint64_t IST6;
    uint64_t IST7;
    uint64_t Reserved2;
    uint16_t Reserved3;
    uint16_t IOMapBase; // I/O map base address
} __attribute__((packed)) TSS64, *PTSS64;

// TSS Descriptor Structure: 16 bytes
typedef struct _TSS_DESCRIPTOR
{
    uint16_t LimitLow;
    uint16_t BaseLow;
    uint8_t BaseMiddle;
    uint8_t Attributes;
    uint8_t LimitHighAndFlags;
    uint8_t BaseHigh;
    uint32_t BaseUpper;
    uint32_t Reserved;
} __attribute__((packed)) TSS_DESCRIPTOR;

// GDT Pointer Structure
typedef struct
{
    uint16_t Limit; // Size of the GDT
    uint64_t Base;  // Base address of the GDT
} __attribute__((packed)) GDT_PTR;

typedef struct
{
    /// Array of GDT entries, including:
    /// 1. Null Descriptor
    /// 2. Kernel Code Segment Descriptor
    /// 3. Kernel Data Segment Descriptor
    /// 4. User Code Segment Descriptor
    /// 5. User Data Segment Descriptor
    /// 6-7. TSS Descriptor (uses two entries)
    GDT_ENTRY GdtEntries[NGDT];

    TSS64 Tss; // Task State Segment

    GDT_PTR GdtPtr; // GDT Pointer
} __attribute__((packed)) CPU_CORE_LOCAL_DATA;

#define GDT_SELECTOR(index, rpl) (((index) << 3) | (rpl))
#define GDT_NULL_SEL             GDT_SELECTOR(0, 0) // Null Descriptor selector
#define GDT_KRNL_CODE_SEL        GDT_SELECTOR(1, 0) // Kernel Code Segment selector
#define GDT_KRNL_DATA_SEL        GDT_SELECTOR(2, 0) // Kernel Data Segment selector
#define GDT_USER_CODE_SEL        GDT_SELECTOR(3, 3) // User Code Segment selector
#define GDT_USER_DATA_SEL        GDT_SELECTOR(4, 3) // User Data Segment selector
#define GDT_TSS_SEL              GDT_SELECTOR(5, 0) // TSS Descriptor selector

//
// Page Table for x64 Long Mode
//

// Each entry is 8 bytes (64 bits)
typedef uint64_t PAGE_TABLE_ENTRY;

#define PAGE_4KB           0x1000ULL
#define PAGE_2MB           0x200000ULL
#define PAGE_1GB           0x40000000ULL
#define PAGE_SHIFT         12
#define ENTRIES_PER_TABLE  512

#define PTE_PRESENT        (1ULL << 0)  // Page present
#define PTE_WRITABLE       (1ULL << 1)  // Read/write
#define PTE_USER           (1ULL << 2)  // User/supervisor
#define PTE_WRITETHROUGH   (1ULL << 3)  // Write-through caching
#define PTE_CACHE_DISABLE  (1ULL << 4)  // Cache disabled
#define PTE_ACCESSED       (1ULL << 5)  // Accessed
#define PTE_DIRTY          (1ULL << 6)  // Dirty (for PDE only in some levels)
#define PTE_PAGE_SIZE      (1ULL << 7)  // Page size (1 for huge pages)
#define PTE_GLOBAL         (1ULL << 8)  // Global page
#define PTE_NO_EXECUTE     (1ULL << 63) // No execute (NXE)

#define PML4_SHIFT         39
#define PDPT_SHIFT         30
#define PD_SHIFT           21
#define PT_SHIFT           12

#define PAGE_MASK          (~(PAGE_4KB - 1))
#define ADDR_MASK_48BIT    0x0000FFFFFFFFFFFFULL // 48-bit address mask

#define PML4_INDEX(addr)   (((addr) >> PML4_SHIFT) & (ENTRIES_PER_TABLE - 1))
#define PDPT_INDEX(addr)   (((addr) >> PDPT_SHIFT) & (ENTRIES_PER_TABLE - 1))
#define PD_INDEX(addr)     (((addr) >> PD_SHIFT) & (ENTRIES_PER_TABLE - 1))
#define PT_INDEX(addr)     (((addr) >> PT_SHIFT) & (ENTRIES_PER_TABLE - 1))

#define PML4_ENTRY_MAPSIZ  0x0000800000000000ULL // Each PML4 entry maps 512GB
#define PDPT_ENTRY_MAPSIZ  0x0000004000000000ULL // Each PDPT entry maps 1GB
#define PD_ENTRY_MAPSIZ    0x00000000200000ULL   // Each PD entry maps 2MB
#define PT_ENTRY_MAPSIZ    0x00000000001000ULL   // Each PT entry maps 4KB
#define PT_TABLE_MAPSIZ    PD_ENTRY_MAPSIZ       // Each PT table maps 2MB
#define PD_TABLE_MAPSIZ    PDPT_ENTRY_MAPSIZ     // Each PD table maps 1GB
#define PDPT_TABLE_MAPSIZ  PML4_ENTRY_MAPSIZ     // Each PDPT table maps 512GB

#define KRNL_BASE_VA       0xFFFF800000000000ULL // Kernel base virtual address
#define KRNL_ENTRY_VA      0xFFFF804000000000ULL // Kernel entry virtual address (1GB offset from base)
#define KRNL_STACK_VA      0xFFFF808000000000ULL // Kernel stack BOTTOM virtual address (2GB offset from base)
/* NOTE: In HimuOS, every stack always includes extra one guard page (no physical memory allocated) to catch stack
 * overflows. */
#define KRNL_IST1_STACK_VA (KRNL_STACK_VA + KRNL_STACK_SIZE + PAGE_4KB) // Kernel IST1 stack BOTTOM virtual address
#define MMIO_BASE_VA       0xFFFFC00000000000ULL                        // MM

/**
 * Calculate the number of pages needed to store a given number of entries.
 * @param entries the number of entries
 * @param entrySize the size of each entry in bytes
 * @param pageSize the size of each page in bytes
 * @return the number of pages required
 */
HO_KERNEL_API uint64_t CalcPagesToStoreEntries(uint64_t entries, uint64_t entrySize, uint64_t pageSize);

HO_KERNEL_API void LoadCR3(HO_PHYSICAL_ADDRESS pml4PhysAddr);

/**
 * @brief Initialize the local data area (Core-Local Data) for a CPU core.
 *
 * @details
 * This function takes ownership of a contiguous memory region and sets it up as a functional `CPU_CORE_LOCAL_DATA`
 * structure. It performs the following operations:
 * 1. Verifies that the memory size is sufficient.
 * 2. Initializes GDT entries (Null, kernel/user code segments, kernel/user data segments).
 * 3. Sets the TSS descriptor in the GDT (two entries) to point to the internal `Tss` member of the structure.
 * 4. Fills the `GdtPtr` member to point to the base address and limit of the GDT table.
 *
 * @note
 * It *does not* fill in the fields inside the TSS (such as RSP0).
 * These fields should be set separately after allocating the stack for this core.
 *
 * @param base  Base address of the contiguous memory block used to store `CPU_CORE_LOCAL_DATA`.
 * @param size  Total size of the memory block (in bytes).
 *
 * @return On success, returns a pointer to the initialized `CPU_CORE_LOCAL_DATA` structure (i.e., `base`).
 * @return On failure (e.g., `size` < `sizeof(CPU_CORE_LOCAL_DATA)`), returns `NULL`.
 */
HO_KERNEL_API HO_NODISCARD CPU_CORE_LOCAL_DATA *InitCpuCoreLocalData(void *base, uint64_t size);

HO_KERNEL_API void LoadGdtAndTss(CPU_CORE_LOCAL_DATA *data);