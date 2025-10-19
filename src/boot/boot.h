/**
 * HimuOperatingSystem
 *
 * File: boot.h
 * Description:
 * Boot information structures and constants.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "kernel/mm/mm_efi.h"

enum VIDEO_MODE_TYPE
{
    VIDEO_MODE_TYPE_UNDEFINED = 0, // Undefined video mode
    VIDEO_MODE_TYPE_UEFI,          // UEFI video mode
    // We have not implemented legacy video mode yet.
    // VIDEO_MODE_TYPE_LEGACY         // Legacy video mode
};

enum PIXEL_FORMAT
{
    PIXEL_FORMAT_RGB, // RGB pixel format
    PIXEL_FORMAT_BGR, // BGR pixel format
};

/**
 * STAGING_BLOCK
 *
 * The "Staging Block" is a temporary and contiguous area in memory used
 * during the very early stages of the kernel's boot process. It acts as an intermediate workspace
 * where the initial kernel code runs to set up the final virtual memory map.
 *
 * Staging Block starts with the `STAGING_BLOCK` structure, more details see also `doc`.
 *
 * The bootloader creates the page tables that establish the "Lower 4GB Identity Mapping" and map
 * this staging block to a higher-half address. The kernel then switches to this higher-half
 * address space and continues its initialization from there.
 *
 * All offsets in this structure are relative to the start of the staging block.
 */
typedef struct
{
    uint64_t Magic;               // 'HOS!' (0x214F5348)
    HO_PHYSICAL_ADDRESS BasePhys; // Physical base address of the staging block
    HO_VIRTUAL_ADDRESS BaseVirt;  // Virtual base address of the staging block
    uint64_t Size;                // Total size of the staging block
    uint64_t TotalReclaimableMem; // Total size of reclaimable memory

    // GOP
    HO_PHYSICAL_ADDRESS FramebufferPhys;
    HO_VIRTUAL_ADDRESS FramebufferVirt;
    enum VIDEO_MODE_TYPE VideoModeType;
    enum PIXEL_FORMAT PixelFormat;
    uint64_t FramebufferSize;
    uint64_t HorizontalResolution;
    uint64_t VerticalResolution;
    uint64_t PixelsPerScanLine;

    uint64_t InfoSize;    // Size of header `STAGING_BLOCK`
    uint64_t GdtSize;       // Size of GDT
    uint64_t MemoryMapSize; // Size of the memory map
    uint64_t PageTableSize; // Size of the page tables
    uint64_t KrnlCodeSize;  // Size of the kernel physical memory occupied by the kernel ELF code segments
    uint64_t KrnlDataSize;  // Size of the kernel physical memory occupied by the kernel ELF data segments (BSS)
    uint64_t KrnlVirtSize;  // Size of the kernel virtual address space occupied by the kernel ELF loaded segments
    uint64_t KrnlStackSize; // Size of the kernel stack, always `KRNL_STACK_SIZE`

    HO_PHYSICAL_ADDRESS GdtPhys;       // Physical address of the GDT
    HO_PHYSICAL_ADDRESS MemoryMapPhys; // Physical address of the memory map
    HO_PHYSICAL_ADDRESS KrnlEntryPhys; // Physical address of the kernel loaded segments
    HO_PHYSICAL_ADDRESS KrnlStackPhys; // Physical address of the kernel stack
    HO_PHYSICAL_ADDRESS PageTablePhys; // Physical address of the page tables

    HO_VIRTUAL_ADDRESS GdtVirt;       // Virtual address of the GDT
    HO_VIRTUAL_ADDRESS MemoryMapVirt; // Virtual address of the memory map
    HO_VIRTUAL_ADDRESS PageTableVirt; // Virtual address of the page tables
    HO_VIRTUAL_ADDRESS KrnlEntryVirt; // Virtual address of the entry of kernel
    HO_VIRTUAL_ADDRESS KrnlStackVirt; // Virtual address of the kernel stack
} STAGING_BLOCK;

typedef STAGING_BLOCK BOOT_INFO_HEADER;

#define STAGING_HEADER_SIZE sizeof(STAGING_BLOCK)
#define STAGING_BLOCK_MAGIC 0x214F5348 // 'HOS!'

#define MIN_MEMMAP_PAGES    3      // 12KB for memory map at least
#define KRNL_STACK_PAGES    4      // 16KB Kernel Stack
#define KRNL_STACK_SIZE     0x4000 // 16KB Kernel Stack
