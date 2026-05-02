/**
 * HimuOperatingSystem
 *
 * File: drivers/input/ps2_keyboard_driver.h
 * Description: Minimal QEMU PS/2 keyboard runtime driver primitives.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

typedef struct PS2_KEYBOARD_DRIVER
{
    BOOL Initialized;
    uint8_t MasterMask;
    uint8_t SlaveMask;
} PS2_KEYBOARD_DRIVER;

HO_KERNEL_API HO_STATUS Ps2KeyboardDriverInit(PS2_KEYBOARD_DRIVER *driver);
HO_KERNEL_API BOOL Ps2KeyboardDriverHasPendingData(const PS2_KEYBOARD_DRIVER *driver);
HO_KERNEL_API HO_STATUS Ps2KeyboardDriverReadScanCode(const PS2_KEYBOARD_DRIVER *driver, uint8_t *outScanCode);
HO_KERNEL_API void Ps2KeyboardDriverAcknowledgeInterrupt(const PS2_KEYBOARD_DRIVER *driver);
