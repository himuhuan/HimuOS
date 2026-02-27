/**
 * HimuOperatingSystem
 *
 * File: drivers/time/lapic_timer_driver.h
 * Description:
 * Local APIC timer driver primitives for x64.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#define IA32_APIC_BASE_MSR       0x1BU
#define IA32_X2APIC_MSR_BASE     0x800U
#define IA32_APIC_BASE_X2APIC    (1ULL << 10)
#define IA32_APIC_BASE_ENABLE    (1ULL << 11)
#define IA32_APIC_BASE_ADDR_MASK 0x00000000FFFFF000ULL

#define LAPIC_REG_EOI            0x0B0U
#define LAPIC_REG_SVR            0x0F0U
#define LAPIC_REG_LVT_TIMER      0x320U
#define LAPIC_REG_INITIAL_COUNT  0x380U
#define LAPIC_REG_CURRENT_COUNT  0x390U
#define LAPIC_REG_DIVIDE_CONFIG  0x3E0U

#define LAPIC_SVR_ENABLE         (1U << 8)

#define LAPIC_LVT_MASKED         (1U << 16)
#define LAPIC_LVT_MODE_ONE_SHOT  (0U << 17)

#define LAPIC_TIMER_DIVIDE_BY_16 0x3U

typedef enum LAPIC_ACCESS_MODE
{
    LAPIC_ACCESS_XAPIC_MMIO = 0,
    LAPIC_ACCESS_X2APIC_MSR = 1
} LAPIC_ACCESS_MODE;

HO_STATUS LapicDetectAndEnable(HO_PHYSICAL_ADDRESS *basePhysOut, HO_VIRTUAL_ADDRESS *baseVirtOut);
uint32_t LapicReadReg(HO_VIRTUAL_ADDRESS baseVirt, uint32_t regOffset);
void LapicWriteReg(HO_VIRTUAL_ADDRESS baseVirt, uint32_t regOffset, uint32_t value);
LAPIC_ACCESS_MODE LapicGetAccessMode(void);
BOOL LapicIsX2ApicActive(void);
void LapicSetSpuriousVector(HO_VIRTUAL_ADDRESS baseVirt, uint8_t vectorNumber);
void LapicSendEoi(HO_VIRTUAL_ADDRESS baseVirt);
void LapicTimerConfigureOneShot(HO_VIRTUAL_ADDRESS baseVirt, uint8_t vectorNumber, uint32_t dividerValue, BOOL masked);
void LapicTimerSetInitialCount(HO_VIRTUAL_ADDRESS baseVirt, uint32_t initialCount);
uint32_t LapicTimerGetCurrentCount(HO_VIRTUAL_ADDRESS baseVirt);
