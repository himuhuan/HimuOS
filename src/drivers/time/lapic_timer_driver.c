/**
 * HimuOperatingSystem
 *
 * File: drivers/time/lapic_timer_driver.c
 * Description:
 * Local APIC timer driver for x64.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <drivers/time/lapic_timer_driver.h>
#include <arch/amd64/asm.h>
#include <kernel/ke/mm.h>

static BOOL LapicDetectByCpuid(void);
static volatile uint32_t *LapicGetRegPtr(HO_VIRTUAL_ADDRESS baseVirt, uint32_t regOffset);

static BOOL
LapicDetectByCpuid(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    cpuid(0x1, &eax, &ebx, &ecx, &edx);
    (void)eax;
    (void)ebx;
    (void)ecx;
    return (edx & (1U << 9)) != 0;
}

static volatile uint32_t *
LapicGetRegPtr(HO_VIRTUAL_ADDRESS baseVirt, uint32_t regOffset)
{
    return (volatile uint32_t *)(baseVirt + regOffset);
}

HO_STATUS
LapicDetectAndEnable(HO_PHYSICAL_ADDRESS *basePhysOut, HO_VIRTUAL_ADDRESS *baseVirtOut)
{
    if (basePhysOut == NULL || baseVirtOut == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!LapicDetectByCpuid())
        return EC_NOT_SUPPORTED;

    uint64_t apicBase = rdmsr(IA32_APIC_BASE_MSR);
    if ((apicBase & IA32_APIC_BASE_X2APIC) != 0)
        return EC_NOT_SUPPORTED;

    apicBase |= IA32_APIC_BASE_ENABLE;
    wrmsr(IA32_APIC_BASE_MSR, apicBase);

    apicBase = rdmsr(IA32_APIC_BASE_MSR);
    if ((apicBase & IA32_APIC_BASE_ENABLE) == 0)
        return EC_FAILURE;

    HO_PHYSICAL_ADDRESS basePhys = apicBase & IA32_APIC_BASE_ADDR_MASK;
    if (basePhys == 0)
        return EC_FAILURE;

    *basePhysOut = basePhys;
    *baseVirtOut = HHDM_PHYS2VIRT(basePhys);
    return EC_SUCCESS;
}

uint32_t
LapicReadReg(HO_VIRTUAL_ADDRESS baseVirt, uint32_t regOffset)
{
    volatile uint32_t *reg = LapicGetRegPtr(baseVirt, regOffset);
    return *reg;
}

void
LapicWriteReg(HO_VIRTUAL_ADDRESS baseVirt, uint32_t regOffset, uint32_t value)
{
    volatile uint32_t *reg = LapicGetRegPtr(baseVirt, regOffset);
    *reg = value;
    __asm__ __volatile__("" ::: "memory");
}

void
LapicSetSpuriousVector(HO_VIRTUAL_ADDRESS baseVirt, uint8_t vectorNumber)
{
    uint32_t svr = LapicReadReg(baseVirt, LAPIC_REG_SVR);
    svr &= ~0xFFU;
    svr |= (uint32_t)vectorNumber;
    svr |= LAPIC_SVR_ENABLE;
    LapicWriteReg(baseVirt, LAPIC_REG_SVR, svr);
}

void
LapicSendEoi(HO_VIRTUAL_ADDRESS baseVirt)
{
    LapicWriteReg(baseVirt, LAPIC_REG_EOI, 0U);
}

void
LapicTimerConfigureOneShot(HO_VIRTUAL_ADDRESS baseVirt, uint8_t vectorNumber, uint32_t dividerValue, BOOL masked)
{
    uint32_t lvt = (uint32_t)vectorNumber | LAPIC_LVT_MODE_ONE_SHOT;

    if (masked)
        lvt |= LAPIC_LVT_MASKED;

    LapicWriteReg(baseVirt, LAPIC_REG_DIVIDE_CONFIG, dividerValue);
    LapicWriteReg(baseVirt, LAPIC_REG_LVT_TIMER, lvt);
}

void
LapicTimerSetInitialCount(HO_VIRTUAL_ADDRESS baseVirt, uint32_t initialCount)
{
    LapicWriteReg(baseVirt, LAPIC_REG_INITIAL_COUNT, initialCount);
}

uint32_t
LapicTimerGetCurrentCount(HO_VIRTUAL_ADDRESS baseVirt)
{
    return LapicReadReg(baseVirt, LAPIC_REG_CURRENT_COUNT);
}
