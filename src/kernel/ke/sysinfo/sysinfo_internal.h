/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo/sysinfo_internal.h
 * Description:
 * Private declarations shared by system-information query modules.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <kernel/ke/sysinfo.h>
#include <kernel/ke/time_source.h>
#include <kernel/ke/clock_event.h>
#include <kernel/ke/scheduler.h>
#include <kernel/hodefs.h>
#include <kernel/init.h>
#include <arch/arch.h>
#include <libc/string.h>

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

#if __HO_DEBUG_BUILD__
HO_STATUS QueryBootMemoryMap(void *Buffer, size_t BufferSize, size_t *RequiredSize);
#endif

HO_STATUS QueryCpuBasic(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryCpuFeatures(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryPageTable(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryPhysicalMemStats(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryVirtualLayout(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryVmmOverview(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryGdt(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryTss(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryIdt(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryTimeSource(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryUptime(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QuerySystemVersion(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryClockEvent(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryScheduler(void *Buffer, size_t BufferSize, size_t *RequiredSize);
