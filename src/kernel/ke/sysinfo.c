/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo.c
 * Description:
 * Ke Layer - System information query API implementation.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <kernel/ke/sysinfo.h>
#include <kernel/ke/time_source.h>
#include <kernel/ke/clock_event.h>
#include <kernel/hodefs.h>
#include <kernel/init.h>
#include <arch/arch.h>
#include <libc/string.h>

// ─────────────────────────────────────────────────────────────
// Internal Helpers
// ─────────────────────────────────────────────────────────────

static inline uint64_t
ReadCr3(void)
{
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

static inline void
StorGdt(GDT_PTR *gdtPtr)
{
    __asm__ volatile("sgdt %0" : "=m"(*gdtPtr));
}

typedef struct
{
    uint16_t Limit;
    uint64_t Base;
} __attribute__((packed)) IDT_PTR;

static inline void
StorIdt(IDT_PTR *idtPtr)
{
    __asm__ volatile("sidt %0" : "=m"(*idtPtr));
}

static inline void
Cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx) : "a"(leaf), "c"(subleaf));
}

// ─────────────────────────────────────────────────────────────
// Query Handlers
// ─────────────────────────────────────────────────────────────

#if __HO_DEBUG_BUILD__
static HO_STATUS
QueryBootMemoryMap(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    BOOT_CAPSULE *capsule = KeGetBootCapsule();
    if (!capsule)
        return EC_INVALID_STATE;

    size_t required = capsule->Layout.MemoryMapSize;

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    HO_VIRTUAL_ADDRESS mapVirt = HHDM_BASE_VA + capsule->MemoryMapPhys;
    memcpy(Buffer, (void *)mapVirt, required);

    return EC_SUCCESS;
}
#endif

static HO_STATUS
QueryCpuBasic(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(ARCH_BASIC_CPU_INFO);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    GetBasicCpuInfo((ARCH_BASIC_CPU_INFO *)Buffer);
    return EC_SUCCESS;
}

static HO_STATUS
QueryCpuFeatures(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_CPU_FEATURES);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_CPU_FEATURES *info = (SYSINFO_CPU_FEATURES *)Buffer;
    uint32_t eax, ebx, ecx, edx;

    // CPUID.01H
    Cpuid(0x01, 0, &eax, &ebx, &ecx, &edx);
    info->Leaf1_ECX = ecx;
    info->Leaf1_EDX = edx;

    // CPUID.07H
    Cpuid(0x07, 0, &eax, &ebx, &ecx, &edx);
    info->Leaf7_EBX = ebx;
    info->Leaf7_ECX = ecx;

    // CPUID.80000001H
    Cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx);
    info->ExtLeaf1_ECX = ecx;
    info->ExtLeaf1_EDX = edx;

    return EC_SUCCESS;
}

static HO_STATUS
QueryPageTable(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_PAGE_TABLE);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_PAGE_TABLE *info = (SYSINFO_PAGE_TABLE *)Buffer;
    info->Cr3 = ReadCr3();

    return EC_SUCCESS;
}

static HO_STATUS
QueryPhysicalMemStats(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    (void)Buffer;
    (void)BufferSize;
    (void)RequiredSize;
    // Reserved for PMM implementation
    return EC_NOT_SUPPORTED;
}

static HO_STATUS
QueryVirtualLayout(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_VIRTUAL_LAYOUT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_VIRTUAL_LAYOUT *info = (SYSINFO_VIRTUAL_LAYOUT *)Buffer;
    info->KernelBase = KRNL_BASE_VA;
    info->KernelStack = KRNL_STACK_VA;
    info->HhdmBase = HHDM_BASE_VA;
    info->MmioBase = MMIO_BASE_VA;

    return EC_SUCCESS;
}

static HO_STATUS
QueryGdt(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_GDT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_GDT *info = (SYSINFO_GDT *)Buffer;

    GDT_PTR gdtPtr;
    StorGdt(&gdtPtr);

    info->Limit = gdtPtr.Limit;
    info->Base = gdtPtr.Base;
    info->EntryCount = NGDT;

    // Copy GDT entries
    GDT_ENTRY *gdtBase = (GDT_ENTRY *)gdtPtr.Base;
    for (uint16_t i = 0; i < NGDT; i++)
    {
        info->Entries[i] = gdtBase[i];
    }

    return EC_SUCCESS;
}

static HO_STATUS
QueryTss(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(TSS64);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    // Get GDT base to find TSS descriptor
    GDT_PTR gdtPtr;
    StorGdt(&gdtPtr);

    // TSS descriptor is at index 5 (16 bytes, spans entries 5-6)
    TSS_DESCRIPTOR *tssDesc = (TSS_DESCRIPTOR *)((uint8_t *)gdtPtr.Base + GDT_TSS_INDEX * sizeof(GDT_ENTRY));

    // Reconstruct TSS base address from descriptor
    uint64_t tssBase = (uint64_t)tssDesc->BaseLow | ((uint64_t)tssDesc->BaseMiddle << 16) |
                       ((uint64_t)tssDesc->BaseHigh << 24) | ((uint64_t)tssDesc->BaseUpper << 32);

    // Copy TSS
    memcpy(Buffer, (void *)tssBase, sizeof(TSS64));

    return EC_SUCCESS;
}

static HO_STATUS
QueryIdt(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_IDT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_IDT *info = (SYSINFO_IDT *)Buffer;

    IDT_PTR idtPtr;
    StorIdt(&idtPtr);

    info->Limit = idtPtr.Limit;
    info->Base = idtPtr.Base;

    return EC_SUCCESS;
}

static HO_STATUS
QueryTimeSource(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_TIME_SOURCE);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    if (!KeIsTimeSourceReady())
        return EC_INVALID_STATE;

    SYSINFO_TIME_SOURCE *info = (SYSINFO_TIME_SOURCE *)Buffer;
    memset(info, 0, sizeof(*info));

    TIME_SOURCE_KIND kind = KeGetTimeSourceKind();
    const char *name;

    switch (kind)
    {
    case TIME_SOURCE_TSC:
        name = "TSC";
        break;
    case TIME_SOURCE_PM_TIMER:
        name = "PM Timer";
        break;
    case TIME_SOURCE_HPET:
        name = "HPET";
        break;
    default:
        name = "Unknown";
        break;
    }

    size_t nameLen = strlen(name);
    if (nameLen >= SYSINFO_TIME_SOURCE_NAME_LEN)
        nameLen = SYSINFO_TIME_SOURCE_NAME_LEN - 1;
    memcpy(info->Name, name, nameLen);
    info->Name[nameLen] = '\0';

    info->Frequency = KeGetTimeSourceFrequency();
    info->Features = 0;

    return EC_SUCCESS;
}

static HO_STATUS
QueryUptime(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_UPTIME);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    if (!KeIsTimeSourceReady())
        return EC_INVALID_STATE;

    SYSINFO_UPTIME *info = (SYSINFO_UPTIME *)Buffer;
    // KeGetSystemUpRealTime returns microseconds, convert to nanoseconds
    info->Nanoseconds = KeGetSystemUpRealTime() * 1000ULL;

    return EC_SUCCESS;
}

static HO_STATUS
QuerySystemVersion(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_SYSTEM_VERSION);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_SYSTEM_VERSION *info = (SYSINFO_SYSTEM_VERSION *)Buffer;
    memset(info, 0, sizeof(*info));

    info->Major = 1;
    info->Minor = 0;
    info->Patch = 0;

    // __DATE__ format: "Mmm dd yyyy" (12 chars with null)
    // __TIME__ format: "hh:mm:ss" (9 chars with null)
    const char *buildDate = __DATE__;
    const char *buildTime = __TIME__;

    size_t dateLen = strlen(buildDate);
    if (dateLen >= sizeof(info->BuildDate))
        dateLen = sizeof(info->BuildDate) - 1;
    memcpy(info->BuildDate, buildDate, dateLen);
    info->BuildDate[dateLen] = '\0';

    size_t timeLen = strlen(buildTime);
    if (timeLen >= sizeof(info->BuildTime))
        timeLen = sizeof(info->BuildTime) - 1;
    memcpy(info->BuildTime, buildTime, timeLen);
    info->BuildTime[timeLen] = '\0';

    return EC_SUCCESS;
}

static HO_STATUS
QueryClockEvent(void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    const size_t required = sizeof(SYSINFO_CLOCK_EVENT);

    if (RequiredSize)
        *RequiredSize = required;

    if (!Buffer)
        return EC_SUCCESS;

    if (BufferSize < required)
        return EC_NOT_ENOUGH_MEMORY;

    SYSINFO_CLOCK_EVENT *info = (SYSINFO_CLOCK_EVENT *)Buffer;
    memset(info, 0, sizeof(*info));

    info->Ready = KeClockEventIsReady();

    if (!info->Ready)
        return EC_SUCCESS;

    info->FreqHz = KeClockEventGetFrequency();
    info->InterruptCount = KeClockEventGetInterruptCount();
    info->VectorNumber = KeClockEventGetVector();

    const char *name = KeClockEventGetSourceName();
    if (name)
    {
        size_t len = strlen(name);
        if (len >= sizeof(info->SourceName))
            len = sizeof(info->SourceName) - 1;
        memcpy(info->SourceName, name, len);
        info->SourceName[len] = '\0';
    }

    return EC_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
// Main API
// ─────────────────────────────────────────────────────────────

HO_STATUS HO_KERNEL_API
KeQuerySystemInformation(KE_SYSINFO_CLASS Class, void *Buffer, size_t BufferSize, size_t *RequiredSize)
{
    if (Class >= KE_SYSINFO_MAX)
        return EC_ILLEGAL_ARGUMENT;

    switch (Class)
    {
    case KE_SYSINFO_BOOT_MEMORY_MAP:
#if __HO_DEBUG_BUILD__
        return QueryBootMemoryMap(Buffer, BufferSize, RequiredSize);
#else
        return EC_NOT_SUPPORTED;
#endif

    case KE_SYSINFO_CPU_BASIC:
        return QueryCpuBasic(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_CPU_FEATURES:
        return QueryCpuFeatures(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_PAGE_TABLE:
        return QueryPageTable(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_PHYSICAL_MEM_STATS:
        return QueryPhysicalMemStats(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_VIRTUAL_LAYOUT:
        return QueryVirtualLayout(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_GDT:
        return QueryGdt(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_TSS:
        return QueryTss(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_IDT:
        return QueryIdt(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_TIME_SOURCE:
        return QueryTimeSource(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_UPTIME:
        return QueryUptime(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_SYSTEM_VERSION:
        return QuerySystemVersion(Buffer, BufferSize, RequiredSize);

    case KE_SYSINFO_CLOCK_EVENT:
        return QueryClockEvent(Buffer, BufferSize, RequiredSize);

    default:
        return EC_ILLEGAL_ARGUMENT;
    }
}
