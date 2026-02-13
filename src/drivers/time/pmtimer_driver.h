/**
 * HimuOperatingSystem
 *
 * File: ke/time/pmtimer_driver.h
 * Description:
 * ACPI PM Timer driver internal header.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

#define PM_TIMER_FREQ_HZ 3579545ULL

HO_STATUS PmTimerSetup(HO_PHYSICAL_ADDRESS acpiRsdpPhys, uint16_t *portOut, uint8_t *bitWidthOut);
uint32_t PmTimerRead(uint16_t port);
