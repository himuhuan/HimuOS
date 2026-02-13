/**
 * HimuOperatingSystem
 *
 * File: ke/time/pmtimer_driver.c
 * Description:
 * ACPI PM Timer driver for x64.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "pmtimer_driver.h"
#include <arch/amd64/acpi.h>
#include <arch/amd64/asm.h>
#include <kernel/ke/mm.h>
#include <libc/string.h>

#define ACPI_GAS_SPACE_SYSTEM_IO 0x01
#define ACPI_FADT_FLAG_TMR_VAL_EXT (1u << 8)

static ACPI_FADT *
AcpiFindFadtTable(HO_PHYSICAL_ADDRESS rsdpPhys)
{
    ACPI_RSDP *rsdp = (ACPI_RSDP *)HHDM_PHYS2VIRT(rsdpPhys);

    if (rsdp->Revision >= 2 && rsdp->XsdtPhys != 0)
    {
        ACPI_SDT_HEADER *xsdt = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(rsdp->XsdtPhys);
        uint32_t entries = (xsdt->Length - sizeof(ACPI_SDT_HEADER)) / 8;
        uint64_t *tableAddrs = (uint64_t *)((uint8_t *)xsdt + sizeof(ACPI_SDT_HEADER));

        for (uint32_t i = 0; i < entries; i++)
        {
            ACPI_SDT_HEADER *table = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(tableAddrs[i]);
            if (memcmp(table->Signature, "FACP", 4) == 0)
                return (ACPI_FADT *)table;
        }
    }

    if (rsdp->RsdtPhys != 0)
    {
        ACPI_SDT_HEADER *rsdt = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(rsdp->RsdtPhys);
        uint32_t entries = (rsdt->Length - sizeof(ACPI_SDT_HEADER)) / 4;
        uint32_t *tableAddrs = (uint32_t *)((uint8_t *)rsdt + sizeof(ACPI_SDT_HEADER));

        for (uint32_t i = 0; i < entries; i++)
        {
            ACPI_SDT_HEADER *table = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(tableAddrs[i]);
            if (memcmp(table->Signature, "FACP", 4) == 0)
                return (ACPI_FADT *)table;
        }
    }

    return NULL;
}

static HO_STATUS
PmTimerExtractFromGas(const ACPI_GAS *gas, uint16_t *portOut)
{
    if (!gas || !portOut)
        return EC_ILLEGAL_ARGUMENT;

    if (gas->AddressSpaceId != ACPI_GAS_SPACE_SYSTEM_IO)
        return EC_NOT_SUPPORTED;

    if (gas->Address == 0 || gas->Address > 0xFFFFULL)
        return EC_NOT_SUPPORTED;

    *portOut = (uint16_t)gas->Address;
    return EC_SUCCESS;
}

HO_STATUS
PmTimerSetup(HO_PHYSICAL_ADDRESS acpiRsdpPhys, uint16_t *portOut, uint8_t *bitWidthOut)
{
    if (!portOut || !bitWidthOut)
        return EC_ILLEGAL_ARGUMENT;

    ACPI_FADT *fadt = AcpiFindFadtTable(acpiRsdpPhys);
    if (fadt == NULL)
        return EC_NOT_SUPPORTED;

    uint16_t port = 0;
    HO_STATUS status = EC_NOT_SUPPORTED;

    if (fadt->XPmTmrBlk.Address != 0)
    {
        status = PmTimerExtractFromGas(&fadt->XPmTmrBlk, &port);
    }

    if (status != EC_SUCCESS)
    {
        if (fadt->PmTmrBlk != 0 && fadt->PmTmrBlk <= 0xFFFFU)
        {
            port = (uint16_t)fadt->PmTmrBlk;
            status = EC_SUCCESS;
        }
    }

    if (status != EC_SUCCESS)
        return EC_NOT_SUPPORTED;

    uint8_t bitWidth = (fadt->Flags & ACPI_FADT_FLAG_TMR_VAL_EXT) ? 32 : 24;
    *portOut = port;
    *bitWidthOut = bitWidth;
    return EC_SUCCESS;
}

uint32_t
PmTimerRead(uint16_t port)
{
    return inl(port);
}
