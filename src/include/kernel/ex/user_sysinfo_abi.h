/**
 * HimuOperatingSystem
 *
 * File: ex/user_sysinfo_abi.h
 * Description: Stable Ex-facing structured sysinfo ABI.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#define EX_SYSINFO_OVERVIEW_VERSION         1U
#define EX_SYSINFO_TEXT_MAX_LENGTH          1024U
#define EX_SYSINFO_OVERVIEW_CPU_NAME_LENGTH 128U
#define EX_SYSINFO_OVERVIEW_SOURCE_NAME_LENGTH 32U

#define EX_SYSINFO_OVERVIEW_VALID_CPU         (1ULL << 0)
#define EX_SYSINFO_OVERVIEW_VALID_MEMORY      (1ULL << 1)
#define EX_SYSINFO_OVERVIEW_VALID_VMM         (1ULL << 2)
#define EX_SYSINFO_OVERVIEW_VALID_SCHEDULER   (1ULL << 3)
#define EX_SYSINFO_OVERVIEW_VALID_UPTIME      (1ULL << 4)
#define EX_SYSINFO_OVERVIEW_VALID_CLOCK       (1ULL << 5)
#define EX_SYSINFO_OVERVIEW_VALID_TIME_SOURCE (1ULL << 6)
#define EX_SYSINFO_OVERVIEW_VALID_VERSION     (1ULL << 7)

typedef enum EX_SYSINFO_CLASS
{
    EX_SYSINFO_CLASS_INVALID = 0,
    EX_SYSINFO_CLASS_OVERVIEW = 1,
    EX_SYSINFO_CLASS_OVERVIEW_TEXT = 2,
    EX_SYSINFO_CLASS_THREAD_LIST = 3,
    EX_SYSINFO_CLASS_THREAD_LIST_TEXT = 4,
    EX_SYSINFO_CLASS_MEMMAP_TEXT = 5,
    EX_SYSINFO_CLASS_PROCESS_LIST = 6,
    EX_SYSINFO_CLASS_PROCESS_LIST_TEXT = 7,
} EX_SYSINFO_CLASS;

#define EX_SYSINFO_THREAD_LIST_VERSION     1U
#define EX_SYSINFO_THREAD_NAME_LENGTH      16U
#define EX_SYSINFO_THREAD_LIST_MAX_ENTRIES 8U

typedef enum EX_SYSINFO_THREAD_STATE
{
    EX_SYSINFO_THREAD_STATE_INVALID = 0,
    EX_SYSINFO_THREAD_STATE_READY = 1,
    EX_SYSINFO_THREAD_STATE_RUNNING = 2,
    EX_SYSINFO_THREAD_STATE_BLOCKED = 3,
    EX_SYSINFO_THREAD_STATE_SLEEPING = 4,
    EX_SYSINFO_THREAD_STATE_TERMINATED = 5,
    EX_SYSINFO_THREAD_STATE_IDLE = 6,
} EX_SYSINFO_THREAD_STATE;

typedef struct EX_SYSINFO_THREAD_ENTRY
{
    uint32_t ThreadId;
    uint32_t State;
    uint32_t Priority;
    char Name[EX_SYSINFO_THREAD_NAME_LENGTH];
} EX_SYSINFO_THREAD_ENTRY;

typedef struct EX_SYSINFO_THREAD_LIST
{
    uint32_t Version;
    uint32_t Size;
    uint32_t TotalCount;
    uint32_t ReturnedCount;
    BOOL Truncated;
    uint8_t Reserved[3];
    EX_SYSINFO_THREAD_ENTRY Entries[EX_SYSINFO_THREAD_LIST_MAX_ENTRIES];
} EX_SYSINFO_THREAD_LIST;

#define EX_SYSINFO_PROCESS_LIST_VERSION     1U
#define EX_SYSINFO_PROCESS_NAME_LENGTH      16U
#define EX_SYSINFO_PROCESS_LIST_MAX_ENTRIES 8U

typedef enum EX_SYSINFO_PROCESS_STATE
{
    EX_SYSINFO_PROCESS_STATE_INVALID = 0,
    EX_SYSINFO_PROCESS_STATE_CREATED = 1,
    EX_SYSINFO_PROCESS_STATE_READY = 2,
    EX_SYSINFO_PROCESS_STATE_RUNNING = 3,
    EX_SYSINFO_PROCESS_STATE_BLOCKED = 4,
    EX_SYSINFO_PROCESS_STATE_SLEEPING = 5,
    EX_SYSINFO_PROCESS_STATE_TERMINATED = 6,
} EX_SYSINFO_PROCESS_STATE;

typedef struct EX_SYSINFO_PROCESS_ENTRY
{
    uint32_t ProcessId;
    uint32_t ParentProcessId;
    uint32_t MainThreadId;
    uint32_t State;
    char Name[EX_SYSINFO_PROCESS_NAME_LENGTH];
} EX_SYSINFO_PROCESS_ENTRY;

typedef struct EX_SYSINFO_PROCESS_LIST
{
    uint32_t Version;
    uint32_t Size;
    uint32_t TotalCount;
    uint32_t ReturnedCount;
    BOOL Truncated;
    uint8_t Reserved[3];
    EX_SYSINFO_PROCESS_ENTRY Entries[EX_SYSINFO_PROCESS_LIST_MAX_ENTRIES];
} EX_SYSINFO_PROCESS_LIST;

typedef struct EX_SYSINFO_OVERVIEW
{
    uint32_t Version;
    uint32_t Size;
    uint64_t ValidMask;
    char CpuModel[EX_SYSINFO_OVERVIEW_CPU_NAME_LENGTH];
    uint64_t PhysicalTotalBytes;
    uint64_t PhysicalFreeBytes;
    uint64_t PhysicalAllocatedBytes;
    uint64_t PhysicalReservedBytes;
    uint64_t StackArenaTotalPages;
    uint64_t StackArenaUsedPages;
    uint64_t HeapArenaTotalPages;
    uint64_t HeapArenaUsedPages;
    uint64_t FixmapTotalSlots;
    uint64_t FixmapActiveSlots;
    uint32_t SchedulerEnabled;
    uint32_t CurrentThreadId;
    uint32_t IdleThreadId;
    uint32_t ReadyQueueDepth;
    uint32_t SleepQueueDepth;
    uint32_t ActiveThreadCount;
    uint64_t UptimeNanoseconds;
    uint32_t ClockReady;
    uint32_t ClockVectorNumber;
    uint64_t ClockFrequencyHz;
    char ClockSourceName[EX_SYSINFO_OVERVIEW_SOURCE_NAME_LENGTH];
    uint64_t TimeSourceFrequencyHz;
    char TimeSourceName[EX_SYSINFO_OVERVIEW_SOURCE_NAME_LENGTH];
    uint16_t SystemMajor;
    uint16_t SystemMinor;
    uint16_t SystemPatch;
    char BuildDate[12];
    char BuildTime[10];
} EX_SYSINFO_OVERVIEW;
