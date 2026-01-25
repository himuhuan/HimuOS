/**
 * HimuOperatingSystem
 *
 * File: ke/time/hpet_driver.c
 * Description:
 * HPET (High Precision Event Timer) driver for x64.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "hpet_driver.h"
#include <arch/amd64/acpi.h>
#include <kernel/ke/mm.h>
#include <libc/string.h>

static uint64_t
HpetReadReg(HO_VIRTUAL_ADDRESS baseVirt, uint32_t offset)
{
    return *(volatile uint64_t *)(baseVirt + offset);
}

static void
HpetWriteReg(HO_VIRTUAL_ADDRESS baseVirt, uint32_t offset, uint64_t value)
{
    *(volatile uint64_t *)(baseVirt + offset) = value;
}

static ACPI_HPET *
AcpiFindHpetTable(HO_PHYSICAL_ADDRESS rsdpPhys)
{
    ACPI_RSDP *rsdp = (ACPI_RSDP *)HHDM_PHYS2VIRT(rsdpPhys);

    // Use XSDT (64-bit pointers) if available
    if (rsdp->Revision >= 2 && rsdp->XsdtPhys != 0)
    {
        ACPI_SDT_HEADER *xsdt = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(rsdp->XsdtPhys);
        uint32_t entries = (xsdt->Length - sizeof(ACPI_SDT_HEADER)) / 8;
        uint64_t *tableAddrs = (uint64_t *)((uint8_t *)xsdt + sizeof(ACPI_SDT_HEADER));

        for (uint32_t i = 0; i < entries; i++)
        {
            ACPI_SDT_HEADER *table = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(tableAddrs[i]);
            if (memcmp(table->Signature, "HPET", 4) == 0)
                return (ACPI_HPET *)table;
        }
    }

    // Fallback to RSDT (32-bit pointers)
    if (rsdp->RsdtPhys != 0)
    {
        ACPI_SDT_HEADER *rsdt = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(rsdp->RsdtPhys);
        uint32_t entries = (rsdt->Length - sizeof(ACPI_SDT_HEADER)) / 4;
        uint32_t *tableAddrs = (uint32_t *)((uint8_t *)rsdt + sizeof(ACPI_SDT_HEADER));

        for (uint32_t i = 0; i < entries; i++)
        {
            ACPI_SDT_HEADER *table = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(tableAddrs[i]);
            if (memcmp(table->Signature, "HPET", 4) == 0)
                return (ACPI_HPET *)table;
        }
    }

    return NULL;
}

HO_STATUS
HpetSetup(HO_PHYSICAL_ADDRESS acpiRsdpPhys, HO_VIRTUAL_ADDRESS *baseVirtOut, uint64_t *freqHzOut)
{
    ACPI_HPET *hpetTable = AcpiFindHpetTable(acpiRsdpPhys);
    if (hpetTable == NULL)
        return EC_NOT_SUPPORTED;

    if (hpetTable->AddressSpaceId != 0)
        return EC_NOT_SUPPORTED;

    HO_VIRTUAL_ADDRESS baseVirt = HHDM_PHYS2VIRT(hpetTable->BaseAddressPhys);
    uint64_t capId = HpetReadReg(baseVirt, HPET_REG_CAP_ID);
    uint32_t periodFs = (uint32_t)(capId >> 32);

    if (periodFs == 0)
        return EC_NOT_SUPPORTED;

    uint64_t freqHz = 1000000000000000ULL / periodFs;

    uint64_t config = HpetReadReg(baseVirt, HPET_REG_CONFIG);
    config |= HPET_CONFIG_ENABLE;
    HpetWriteReg(baseVirt, HPET_REG_CONFIG, config);

    *baseVirtOut = baseVirt;
    *freqHzOut = freqHz;
    return EC_SUCCESS;
}

uint64_t
HpetReadMainCounterAt(HO_VIRTUAL_ADDRESS baseVirt)
{
    return HpetReadReg(baseVirt, HPET_REG_MAIN_COUNTER);
}
