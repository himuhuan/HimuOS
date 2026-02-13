/**
 * HimuOperatingSystem
 *
 * File: ke/time/lapic_timer_driver.c
 * Description:
 * Local APIC timer driver for x64.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "lapic_timer_driver.h"

#include <arch/amd64/acpi.h>
#include <arch/amd64/asm.h>
#include <arch/amd64/pm.h>
#include <kernel/ke/mm.h>
#include <libc/string.h>

static ACPI_SDT_HEADER *AcpiFindTable(HO_PHYSICAL_ADDRESS rsdpPhys, const char signature[4]);
static HO_PHYSICAL_ADDRESS MadtGetLocalApicBase(const ACPI_MADT *madt);

static ACPI_SDT_HEADER *
AcpiFindTable(HO_PHYSICAL_ADDRESS rsdpPhys, const char signature[4])
{
    ACPI_RSDP *rsdp = (ACPI_RSDP *)HHDM_PHYS2VIRT(rsdpPhys);

    if (rsdp->Revision >= 2 && rsdp->XsdtPhys != 0)
    {
        ACPI_SDT_HEADER *xsdt = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(rsdp->XsdtPhys);
        uint32_t entryCount = (xsdt->Length - sizeof(ACPI_SDT_HEADER)) / sizeof(uint64_t);
        uint64_t *tableAddrs = (uint64_t *)((uint8_t *)xsdt + sizeof(ACPI_SDT_HEADER));

        for (uint32_t i = 0; i < entryCount; i++)
        {
            ACPI_SDT_HEADER *table = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(tableAddrs[i]);
            if (memcmp(table->Signature, signature, 4) == 0)
            {
                return table;
            }
        }
    }

    if (rsdp->RsdtPhys != 0)
    {
        ACPI_SDT_HEADER *rsdt = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(rsdp->RsdtPhys);
        uint32_t entryCount = (rsdt->Length - sizeof(ACPI_SDT_HEADER)) / sizeof(uint32_t);
        uint32_t *tableAddrs = (uint32_t *)((uint8_t *)rsdt + sizeof(ACPI_SDT_HEADER));

        for (uint32_t i = 0; i < entryCount; i++)
        {
            ACPI_SDT_HEADER *table = (ACPI_SDT_HEADER *)HHDM_PHYS2VIRT(tableAddrs[i]);
            if (memcmp(table->Signature, signature, 4) == 0)
            {
                return table;
            }
        }
    }

    return NULL;
}

static HO_PHYSICAL_ADDRESS
MadtGetLocalApicBase(const ACPI_MADT *madt)
{
    if (madt == NULL || madt->Header.Length < sizeof(ACPI_MADT))
    {
        return 0;
    }

    HO_PHYSICAL_ADDRESS localApicBasePhys = HO_ALIGN_DOWN((HO_PHYSICAL_ADDRESS)madt->LocalApicAddress, PAGE_4KB);
    uint8_t *cursor = (uint8_t *)madt + sizeof(ACPI_MADT);
    uint8_t *tableEnd = (uint8_t *)madt + madt->Header.Length;

    while (cursor + sizeof(ACPI_MADT_ENTRY_HEADER) <= tableEnd)
    {
        ACPI_MADT_ENTRY_HEADER *entry = (ACPI_MADT_ENTRY_HEADER *)cursor;
        if (entry->Length < sizeof(ACPI_MADT_ENTRY_HEADER))
        {
            break;
        }

        if (cursor + entry->Length > tableEnd)
        {
            break;
        }

        if (entry->Type == ACPI_MADT_ENTRY_LOCAL_APIC_ADDR_OVERRIDE &&
            entry->Length >= sizeof(ACPI_MADT_LOCAL_APIC_ADDR_OVERRIDE))
        {
            ACPI_MADT_LOCAL_APIC_ADDR_OVERRIDE *overrideEntry = (ACPI_MADT_LOCAL_APIC_ADDR_OVERRIDE *)entry;
            if (overrideEntry->LocalApicAddress != 0)
            {
                localApicBasePhys = HO_ALIGN_DOWN((HO_PHYSICAL_ADDRESS)overrideEntry->LocalApicAddress, PAGE_4KB);
            }
        }

        cursor += entry->Length;
    }

    return localApicBasePhys;
}

HO_STATUS
LapicTimerDetectBaseFromAcpi(HO_PHYSICAL_ADDRESS acpiRsdpPhys, HO_PHYSICAL_ADDRESS *basePhysOut)
{
    if (basePhysOut == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    *basePhysOut = 0;

    if (acpiRsdpPhys == 0)
    {
        return EC_NOT_SUPPORTED;
    }

    ACPI_SDT_HEADER *table = AcpiFindTable(acpiRsdpPhys, "APIC");
    if (table == NULL || table->Length < sizeof(ACPI_MADT))
    {
        return EC_NOT_SUPPORTED;
    }

    HO_PHYSICAL_ADDRESS localApicBasePhys = MadtGetLocalApicBase((ACPI_MADT *)table);
    if (localApicBasePhys == 0)
    {
        return EC_NOT_SUPPORTED;
    }

    *basePhysOut = localApicBasePhys;
    return EC_SUCCESS;
}

HO_STATUS
LapicTimerEnableAndGetBase(HO_PHYSICAL_ADDRESS preferredBasePhys, HO_PHYSICAL_ADDRESS *basePhysOut)
{
    if (basePhysOut == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    if (!LapicTimerIsSupported())
    {
        return EC_NOT_SUPPORTED;
    }

    uint64_t apicBaseMsr = rdmsr(IA32_APIC_BASE_MSR);
    if (apicBaseMsr & IA32_APIC_BASE_MSR_X2APIC_ENABLE)
    {
        return EC_NOT_SUPPORTED;
    }

    if (preferredBasePhys != 0)
    {
        HO_PHYSICAL_ADDRESS alignedBasePhys = HO_ALIGN_DOWN(preferredBasePhys, PAGE_4KB);
        apicBaseMsr &= ~IA32_APIC_BASE_MSR_ADDR_MASK;
        apicBaseMsr |= (uint64_t)(alignedBasePhys & IA32_APIC_BASE_MSR_ADDR_MASK);
    }

    apicBaseMsr |= IA32_APIC_BASE_MSR_ENABLE;
    wrmsr(IA32_APIC_BASE_MSR, apicBaseMsr);

    uint64_t enabledApicBaseMsr = rdmsr(IA32_APIC_BASE_MSR);
    if ((enabledApicBaseMsr & IA32_APIC_BASE_MSR_ENABLE) == 0)
    {
        return EC_FAILURE;
    }

    if (enabledApicBaseMsr & IA32_APIC_BASE_MSR_X2APIC_ENABLE)
    {
        return EC_NOT_SUPPORTED;
    }

    HO_PHYSICAL_ADDRESS localApicBasePhys = (HO_PHYSICAL_ADDRESS)(enabledApicBaseMsr & IA32_APIC_BASE_MSR_ADDR_MASK);
    if (localApicBasePhys == 0)
    {
        return EC_FAILURE;
    }

    *basePhysOut = localApicBasePhys;
    return EC_SUCCESS;
}

BOOL
LapicTimerIsSupported(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x1, &eax, &ebx, &ecx, &edx);
    (void)eax;
    (void)ebx;
    (void)ecx;
    return (edx & (1U << 9)) != 0;
}

void
LapicMaskLegacyPic(void)
{
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

uint32_t
LapicReadReg32(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint32_t offset)
{
    return *(volatile uint32_t *)(lapicBaseVirt + offset);
}

void
LapicWriteReg32(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint32_t offset, uint32_t value)
{
    volatile uint32_t *reg = (volatile uint32_t *)(lapicBaseVirt + offset);
    *reg = value;
    (void)*reg;
}

void
LapicSetSpuriousVector(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint8_t vectorNumber)
{
    uint32_t svrValue = LapicReadReg32(lapicBaseVirt, LAPIC_REG_SVR);
    svrValue &= ~0xFFU;
    svrValue |= vectorNumber;
    svrValue |= LAPIC_SVR_ENABLE;
    LapicWriteReg32(lapicBaseVirt, LAPIC_REG_SVR, svrValue);
}

void
LapicTimerSetDivide(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint32_t divideCode)
{
    LapicWriteReg32(lapicBaseVirt, LAPIC_REG_DIVIDE_CONFIG, divideCode);
}

void
LapicTimerConfigure(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint8_t vectorNumber, uint32_t mode, BOOL masked)
{
    uint32_t lvtValue = (uint32_t)vectorNumber | mode;
    if (masked)
    {
        lvtValue |= LAPIC_LVT_MASKED;
    }
    LapicWriteReg32(lapicBaseVirt, LAPIC_REG_LVT_TIMER, lvtValue);
}

void
LapicTimerSetInitialCount(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint32_t initialCount)
{
    LapicWriteReg32(lapicBaseVirt, LAPIC_REG_INITIAL_COUNT, initialCount);
}

uint32_t
LapicTimerReadCurrentCount(HO_VIRTUAL_ADDRESS lapicBaseVirt)
{
    return LapicReadReg32(lapicBaseVirt, LAPIC_REG_CURRENT_COUNT);
}

void
LapicSendEoi(HO_VIRTUAL_ADDRESS lapicBaseVirt)
{
    LapicWriteReg32(lapicBaseVirt, LAPIC_REG_EOI, 0);
}
