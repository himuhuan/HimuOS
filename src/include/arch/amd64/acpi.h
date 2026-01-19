/**
 * HimuOperatingSystem
 *
 * File: acpi.h
 * Description:
 * Minimal ACPI structure definitions for AMD64.
 *
 * Only for AMD64 architecture.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

typedef struct __attribute__((packed)) ACPI_RSDP
{
    char Signature[8];
    uint8_t Checksum;
    char OemId[6];
    uint8_t Revision;
    uint32_t RsdtPhys;
    uint32_t Length;
    uint64_t XsdtPhys;
    uint8_t ExtendedChecksum;
    uint8_t Reserved[3];
} ACPI_RSDP;

typedef struct __attribute__((packed)) ACPI_SDT_HEADER
{
    char Signature[4];
    uint32_t Length;
    uint8_t Revision;
    uint8_t Checksum;
    char OemId[6];
    char OemTableId[8];
    uint32_t OemRevision;
    uint32_t CreatorId;
    uint32_t CreatorRevision;
} ACPI_SDT_HEADER;
