/**
 * HimuOperatingSystem
 *
 * File: ke/sysinfo_internal.h
 * Description: Internal declarations for sysinfo query modules.
 *
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

HO_STATUS QueryCpuBasic(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryCpuFeatures(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryGdt(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryTss(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryIdt(void *Buffer, size_t BufferSize, size_t *RequiredSize);

#if __HO_DEBUG_BUILD__
HO_STATUS QueryBootMemoryMap(void *Buffer, size_t BufferSize, size_t *RequiredSize);
#endif
HO_STATUS QueryPageTable(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryPhysicalMemStats(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryVirtualLayout(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QuerySystemVersion(void *Buffer, size_t BufferSize, size_t *RequiredSize);

HO_STATUS QueryTimeSource(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryUptime(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryClockEvent(void *Buffer, size_t BufferSize, size_t *RequiredSize);
HO_STATUS QueryScheduler(void *Buffer, size_t BufferSize, size_t *RequiredSize);
