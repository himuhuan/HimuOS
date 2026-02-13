/**
 * HimuOperatingSystem
 *
 * File: ke/time/lapic_timer_driver.h
 * Description:
 * Local APIC timer driver for x64.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#define IA32_APIC_BASE_MSR               0x1B
#define IA32_APIC_BASE_MSR_ENABLE        (1ULL << 11)
#define IA32_APIC_BASE_MSR_X2APIC_ENABLE (1ULL << 10)
#define IA32_APIC_BASE_MSR_ADDR_MASK     0xFFFFFFFFFF000ULL

#define LAPIC_REG_EOI                    0x0B0
#define LAPIC_REG_SVR                    0x0F0
#define LAPIC_REG_LVT_TIMER              0x320
#define LAPIC_REG_INITIAL_COUNT          0x380
#define LAPIC_REG_CURRENT_COUNT          0x390
#define LAPIC_REG_DIVIDE_CONFIG          0x3E0

#define LAPIC_SVR_ENABLE                 (1U << 8)
#define LAPIC_LVT_MASKED                 (1U << 16)

#define LAPIC_TIMER_MODE_ONE_SHOT        (0U << 17)
#define LAPIC_TIMER_MODE_PERIODIC        (1U << 17)
#define LAPIC_TIMER_MODE_TSC_DEADLINE    (2U << 17)

#define LAPIC_TIMER_DIVIDE_BY_1          0xB
#define LAPIC_TIMER_DIVIDE_BY_2          0x0
#define LAPIC_TIMER_DIVIDE_BY_4          0x1
#define LAPIC_TIMER_DIVIDE_BY_8          0x2
#define LAPIC_TIMER_DIVIDE_BY_16         0x3
#define LAPIC_TIMER_DIVIDE_BY_32         0x8
#define LAPIC_TIMER_DIVIDE_BY_64         0x9
#define LAPIC_TIMER_DIVIDE_BY_128        0xA

HO_STATUS LapicTimerDetectBaseFromAcpi(HO_PHYSICAL_ADDRESS acpiRsdpPhys, HO_PHYSICAL_ADDRESS *basePhysOut);
HO_STATUS LapicTimerEnableAndGetBase(HO_PHYSICAL_ADDRESS preferredBasePhys, HO_PHYSICAL_ADDRESS *basePhysOut);
BOOL LapicTimerIsSupported(void);

void LapicMaskLegacyPic(void);
uint32_t LapicReadReg32(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint32_t offset);
void LapicWriteReg32(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint32_t offset, uint32_t value);
void LapicSetSpuriousVector(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint8_t vectorNumber);
void LapicTimerSetDivide(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint32_t divideCode);
void LapicTimerConfigure(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint8_t vectorNumber, uint32_t mode, BOOL masked);
void LapicTimerSetInitialCount(HO_VIRTUAL_ADDRESS lapicBaseVirt, uint32_t initialCount);
uint32_t LapicTimerReadCurrentCount(HO_VIRTUAL_ADDRESS lapicBaseVirt);
void LapicSendEoi(HO_VIRTUAL_ADDRESS lapicBaseVirt);
