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

typedef struct __attribute__((packed)) ACPI_GAS
{
    uint8_t AddressSpaceId;
    uint8_t RegisterBitWidth;
    uint8_t RegisterBitOffset;
    uint8_t AccessSize;
    uint64_t Address;
} ACPI_GAS;

// Minimal FADT layout (ACPI 2.0+), only fields required for PM Timer.
typedef struct __attribute__((packed)) ACPI_FADT
{
    ACPI_SDT_HEADER Header;
    uint32_t FirmwareCtrl;
    uint32_t Dsdt;
    uint8_t Reserved1;
    uint8_t PreferredPmProfile;
    uint16_t SciInt;
    uint32_t SmiCmd;
    uint8_t AcpiEnable;
    uint8_t AcpiDisable;
    uint8_t S4BiosReq;
    uint8_t PstateCnt;
    uint32_t Pm1aEvtBlk;
    uint32_t Pm1bEvtBlk;
    uint32_t Pm1aCntBlk;
    uint32_t Pm1bCntBlk;
    uint32_t Pm2CntBlk;
    uint32_t PmTmrBlk;
    uint32_t Gpe0Blk;
    uint32_t Gpe1Blk;
    uint8_t Pm1EvtLen;
    uint8_t Pm1CntLen;
    uint8_t Pm2CntLen;
    uint8_t PmTmrLen;
    uint8_t Gpe0BlkLen;
    uint8_t Gpe1BlkLen;
    uint8_t Gpe1Base;
    uint8_t CstCnt;
    uint16_t PLvl2Lat;
    uint16_t PLvl3Lat;
    uint16_t FlushSize;
    uint16_t FlushStride;
    uint8_t DutyOffset;
    uint8_t DutyWidth;
    uint8_t DayAlrm;
    uint8_t MonAlrm;
    uint8_t Century;
    uint16_t IaPcBootArch;
    uint8_t Reserved2;
    uint32_t Flags;
    ACPI_GAS ResetReg;
    uint8_t ResetValue;
    uint16_t ArmBootArch;
    uint8_t FadtMinorVersion;
    uint64_t XFirmwareCtrl;
    uint64_t XDsdt;
    ACPI_GAS XPm1aEvtBlk;
    ACPI_GAS XPm1bEvtBlk;
    ACPI_GAS XPm1aCntBlk;
    ACPI_GAS XPm1bCntBlk;
    ACPI_GAS XPm2CntBlk;
    ACPI_GAS XPmTmrBlk;
    ACPI_GAS XGpe0Blk;
    ACPI_GAS XGpe1Blk;
} ACPI_FADT;

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
