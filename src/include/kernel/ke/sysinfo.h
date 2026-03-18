/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo.h
 * Description:
 * Ke Layer - System information query API
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>
#include <arch/amd64/pm.h>
#include <arch/arch.h>

// ─────────────────────────────────────────────────────────────
// Information Class Enumeration
// ─────────────────────────────────────────────────────────────

typedef enum KE_SYSINFO_CLASS
{
    KE_SYSINFO_BOOT_MEMORY_MAP = 0,
    KE_SYSINFO_CPU_BASIC = 1,
    KE_SYSINFO_CPU_FEATURES = 2,
    KE_SYSINFO_PAGE_TABLE = 3,
    KE_SYSINFO_PHYSICAL_MEM_STATS = 4,
    KE_SYSINFO_VIRTUAL_LAYOUT = 5,
    KE_SYSINFO_GDT = 6,
    KE_SYSINFO_TSS = 7,
    KE_SYSINFO_IDT = 8,
    KE_SYSINFO_TIME_SOURCE = 9,
    KE_SYSINFO_UPTIME = 10,
    KE_SYSINFO_SYSTEM_VERSION = 11,
    KE_SYSINFO_CLOCK_EVENT = 12,
    KE_SYSINFO_MAX
} KE_SYSINFO_CLASS;

// ─────────────────────────────────────────────────────────────
// Return Structures
// ─────────────────────────────────────────────────────────────

// KE_SYSINFO_CPU_FEATURES
typedef struct SYSINFO_CPU_FEATURES
{
    uint32_t Leaf1_ECX;    // CPUID.01H:ECX
    uint32_t Leaf1_EDX;    // CPUID.01H:EDX
    uint32_t Leaf7_EBX;    // CPUID.07H:EBX
    uint32_t Leaf7_ECX;    // CPUID.07H:ECX
    uint32_t ExtLeaf1_ECX; // CPUID.80000001H:ECX
    uint32_t ExtLeaf1_EDX; // CPUID.80000001H:EDX
} SYSINFO_CPU_FEATURES;

// KE_SYSINFO_PAGE_TABLE
typedef struct SYSINFO_PAGE_TABLE
{
    HO_PHYSICAL_ADDRESS Cr3;
} SYSINFO_PAGE_TABLE;

// KE_SYSINFO_PHYSICAL_MEM_STATS (reserved for PMM)
typedef struct SYSINFO_PHYSICAL_MEM_STATS
{
    uint64_t TotalBytes;
    uint64_t UsedBytes;
    uint64_t FreeBytes;
} SYSINFO_PHYSICAL_MEM_STATS;

// KE_SYSINFO_VIRTUAL_LAYOUT
typedef struct SYSINFO_VIRTUAL_LAYOUT
{
    HO_VIRTUAL_ADDRESS KernelBase;
    HO_VIRTUAL_ADDRESS KernelStack;
    HO_VIRTUAL_ADDRESS HhdmBase;
    HO_VIRTUAL_ADDRESS MmioBase;
} SYSINFO_VIRTUAL_LAYOUT;

// KE_SYSINFO_GDT
typedef struct SYSINFO_GDT
{
    uint16_t Limit;
    uint64_t Base;
    uint16_t EntryCount;
    GDT_ENTRY Entries[NGDT];
} SYSINFO_GDT;

// KE_SYSINFO_IDT
typedef struct SYSINFO_IDT
{
    uint16_t Limit;
    uint64_t Base;
} SYSINFO_IDT;

// KE_SYSINFO_TIME_SOURCE
#define SYSINFO_TIME_SOURCE_NAME_LEN 32

typedef struct SYSINFO_TIME_SOURCE
{
    char Name[SYSINFO_TIME_SOURCE_NAME_LEN];
    uint64_t Frequency;
    uint32_t Features;
} SYSINFO_TIME_SOURCE;

// KE_SYSINFO_UPTIME
typedef struct SYSINFO_UPTIME
{
    uint64_t Nanoseconds;
} SYSINFO_UPTIME;

// KE_SYSINFO_SYSTEM_VERSION
typedef struct SYSINFO_SYSTEM_VERSION
{
    uint16_t Major;
    uint16_t Minor;
    uint16_t Patch;
    char BuildDate[12];
    char BuildTime[10];
} SYSINFO_SYSTEM_VERSION;

// KE_SYSINFO_CLOCK_EVENT
typedef struct SYSINFO_CLOCK_EVENT
{
    BOOL Ready;
    uint64_t FreqHz;
    uint64_t InterruptCount;
    uint8_t VectorNumber;
    char SourceName[SYSINFO_TIME_SOURCE_NAME_LEN];
} SYSINFO_CLOCK_EVENT;

// ─────────────────────────────────────────────────────────────
// API Function
// ─────────────────────────────────────────────────────────────

/**
 * @brief Query system information.
 *
 * @param Class The information class to query.
 * @param Buffer Output buffer to receive the information. Can be NULL to query required size.
 * @param BufferSize Size of the output buffer in bytes.
 * @param RequiredSize Optional pointer to receive the required buffer size.
 *
 * @return EC_SUCCESS on success.
 * @return EC_ILLEGAL_ARGUMENT if Class is invalid.
 * @return EC_NOT_ENOUGH_MEMORY if BufferSize is too small (RequiredSize will contain needed size).
 * @return EC_NOT_SUPPORTED if the class is not available in current build configuration.
 */
HO_STATUS HO_KERNEL_API KeQuerySystemInformation(KE_SYSINFO_CLASS Class, void *Buffer, size_t BufferSize,
                                                 size_t *RequiredSize);
