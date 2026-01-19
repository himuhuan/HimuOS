/**
 * HimuOperatingSystem
 *
 * File: boot_capsule.h
 * Description:
 * Boot capsule containing information passed from the bootloader to the kernel.
 *
 * Only for AMD64 architecture.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "arch/amd64/efi_min.h"
#include "arch/amd64/pm.h"

#define BOOT_FRAMEBUFFER_VA     MMIO_BASE_VA
#define BOOT_KRNL_ENTRY_VA      KRNL_ENTRY_VA
#define BOOT_KRNL_STACK_VA      KRNL_STACK_VA      // Kernel stack BOTTOM virtual address
#define BOOT_KRNL_IST1_STACK_VA KRNL_IST1_STACK_VA // Kernel IST1 stack BOTTOM virtual address
#define BOOT_HHDM_BASE_VA       HHDM_BASE_VA

#define BOOT_CAPSULE_MAGIC      0x214F5348 // 'HOS!'

typedef struct BOOT_CAPSULE_LAYOUT
{
    size_t HeaderSize;    // Size of the BOOT_CAPSULE structure itself.
    size_t MemoryMapSize; // Size of the memory map
    size_t KrnlCodeSize;  // Size of the kernel physical memory occupied by the kernel ELF code segments
    size_t KrnlDataSize;  // Size of the kernel physical memory occupied by the kernel ELF data segments (BSS)
    size_t KrnlStackSize; // Size of the kernel stack
    size_t IST1StackSize; // Size of the kernel IST#1 stack
} BOOT_CAPSULE_LAYOUT;

typedef struct BOOT_CAPSULE_PAGE_LAYOUT
{
    UINT64 HeaderWithMapPages; // Pages for BOOT_CAPSULE header plus memory map (aligned to 4KB)
    UINT64 KrnlPages;          // Pages for kernel code + data (aligned to 4KB)
    UINT64 KrnlStackPages;     // Pages for kernel stack (aligned to 4KB)
    UINT64 IST1StackPages;     // Pages for IST#1 stack (aligned to 4KB)
    UINT64 TotalPages;         // Total pages required for the capsule (sum of above)
} BOOT_CAPSULE_PAGE_LAYOUT;

/**
 * @brief Boot capsule containing information passed from the bootloader to the kernel.
 *
 * This structure encapsulates all platform and boot-time data that the kernel needs
 * to initialize system services and device drivers. The bootloader is responsible for
 * populating this capsule before transferring control to the kernel; the kernel reads
 * it during early initialization and may copy or parse its contents as needed.
 *
 * Note: Only physical addresses are exposed in this structure.
 * The kernel is responsible for constructing its own virtual mappings based on these physical addresses.
 *
 * @see bootloader documentation for the exact field layout and initialization contract.
 */
typedef struct BOOT_CAPSULE
{
    uint64_t Magic;               // 'HOS!' (0x214F5348)
    HO_PHYSICAL_ADDRESS BasePhys; // Physical base address of this structure

    // GOP
    HO_PHYSICAL_ADDRESS FramebufferPhys; // Map to BOOT_FRAMEBUFFER_VA
    enum VIDEO_MODE_TYPE VideoModeType;
    enum PIXEL_FORMAT PixelFormat;
    size_t FramebufferSize;
    uint64_t HorizontalResolution;
    uint64_t VerticalResolution;
    uint64_t PixelsPerScanLine;

    BOOT_CAPSULE_LAYOUT Layout;
    BOOT_CAPSULE_PAGE_LAYOUT PageLayout; // Actual page layout used

    HO_PHYSICAL_ADDRESS MemoryMapPhys;     // Physical address of the memory map, HHDM
    HO_PHYSICAL_ADDRESS AcpiRsdpPhys;      // Physical address of ACPI RSDP, HHDM
    HO_PHYSICAL_ADDRESS KrnlEntryPhys;     // Physical address of the kernel loaded segments, BOOT_KRNL_ENTRY_VA
    HO_PHYSICAL_ADDRESS KrnlStackPhys;     // Physical address of the kernel stack, BOOT_KRNL_STACK_VA
    HO_PHYSICAL_ADDRESS KrnlIST1StackPhys; // Physical address of the IST#1 stack, BOOT_KRNL_IST1_STACK_VA

    PAGE_TABLE_INFO PageTableInfo;
    CPU_CORE_LOCAL_DATA CpuInfo;
} BOOT_CAPSULE, STAGING_BLOCK, BOOT_INFO_HEADER;
