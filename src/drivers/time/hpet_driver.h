/**
 * HimuOperatingSystem
 *
 * File: ke/time/hpet_driver.h
 * Description:
 * HPET driver internal header.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#define HPET_REG_CAP_ID       0x000
#define HPET_REG_CONFIG       0x010
#define HPET_REG_MAIN_COUNTER 0x0F0

#define HPET_CONFIG_ENABLE    (1ULL << 0)

HO_STATUS HpetSetup(HO_PHYSICAL_ADDRESS acpiRsdpPhys, HO_VIRTUAL_ADDRESS *baseVirtOut, uint64_t *freqHzOut);
uint64_t HpetReadMainCounterAt(HO_VIRTUAL_ADDRESS baseVirt);
