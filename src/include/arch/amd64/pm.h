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

#define NGDT 6 // the # of GDT entries

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
    uint8_t Type;
    uint8_t FlagsLimitHigh;
    uint8_t BaseHigh;
    uint32_t BaseUpper;
    uint64_t Reserved;
} __attribute__((packed)) TSS_DESCRIPTOR, *PTSS_DESCRIPTOR;

// GDT Pointer Structure
typedef struct
{
    uint16_t Limit; // Size of the GDT
    uint64_t Base;  // Base address of the GDT
} __attribute__((packed)) GDT_PTR;

typedef struct
{
    GDT_PTR GdtPtr;
    /// Array of GDT entries, including:
    /// 1. Null Descriptor
    /// 2. Kernel Code Segment Descriptor
    /// 3. Kernel Data Segment Descriptor
    /// 4. User Code Segment Descriptor
    /// 5.User Data Segment Descriptor
    GDT_ENTRY GdtEntries[NGDT];
} GLOBAL_DESCRIPTOR_TABLE;

#define GDT_SELECTOR(index, rpl) (((index) << 3) | (rpl))
#define GDT_NULL_SEL             GDT_SELECTOR(0, 0) // Null Descriptor selector
#define GDT_KRNL_CODE_SEL        GDT_SELECTOR(1, 0) // Kernel Code Segment selector
#define GDT_KRNL_DATA_SEL        GDT_SELECTOR(2, 0) // Kernel Data Segment selector
#define GDT_USER_CODE_SEL        GDT_SELECTOR(3, 3) // User Code Segment selector
#define GDT_USER_DATA_SEL        GDT_SELECTOR(4, 3) // User Data Segment selector

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
/* NOTE: In HimuOS, every stack always includes extra one guard page (no physical memory allocated) to catch stack overflows. */
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
 * Setup the Global Descriptor Table (GDT).
 *
 * @remarks This function converts the provided contiguous memory region into a `GLOBAL_DESCRIPTOR_TABLE`,
 * and initializes it with standard segment descriptors.
 * @see GLOBAL_DESCRIPTOR_TABLE
 * @param base The base address of the memory region.
 * @param size The size of the memory region in bytes.
 * @return A pointer to the initialized `GLOBAL_DESCRIPTOR_TABLE` if successful; otherwise, `NULL` if the memory region
 * is insufficient.
 */
HO_KERNEL_API HO_NODISCARD GLOBAL_DESCRIPTOR_TABLE *SetupGdt(void *base, uint64_t size);
