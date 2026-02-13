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

typedef struct __attribute__((packed)) ACPI_HPET
{
    ACPI_SDT_HEADER Header;
    uint32_t EventTimerBlockId;
    uint8_t AddressSpaceId;
    uint8_t RegisterBitWidth;
    uint8_t RegisterBitOffset;
    uint8_t Reserved;
    uint64_t BaseAddressPhys;
    uint8_t HpetNumber;
    uint16_t MinimumTick;
    uint8_t PageProtection;
} ACPI_HPET;

typedef struct __attribute__((packed)) ACPI_MADT
{
    ACPI_SDT_HEADER Header;
    uint32_t LocalApicAddress;
    uint32_t Flags;
} ACPI_MADT;

typedef struct __attribute__((packed)) ACPI_MADT_ENTRY_HEADER
{
    uint8_t Type;
    uint8_t Length;
} ACPI_MADT_ENTRY_HEADER;

typedef struct __attribute__((packed)) ACPI_MADT_LOCAL_APIC
{
    ACPI_MADT_ENTRY_HEADER Header;
    uint8_t ProcessorUid;
    uint8_t ApicId;
    uint32_t Flags;
} ACPI_MADT_LOCAL_APIC;

typedef struct __attribute__((packed)) ACPI_MADT_LOCAL_APIC_ADDR_OVERRIDE
{
    ACPI_MADT_ENTRY_HEADER Header;
    uint16_t Reserved;
    uint64_t LocalApicAddress;
} ACPI_MADT_LOCAL_APIC_ADDR_OVERRIDE;

#define ACPI_MADT_ENTRY_LOCAL_APIC               0
#define ACPI_MADT_ENTRY_LOCAL_APIC_ADDR_OVERRIDE 5
