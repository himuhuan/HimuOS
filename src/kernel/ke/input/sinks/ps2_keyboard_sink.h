/**
 * HimuOperatingSystem
 *
 * File: ke/input/sinks/ps2_keyboard_sink.h
 * Description: Ke input sink adapter for the minimal PS/2 keyboard driver.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <drivers/input/ps2_keyboard_driver.h>
#include <kernel/ke/sinks/input_sink.h>
#include <libc/string.h>

typedef struct KE_PS2_KEYBOARD_SINK
{
    KE_INPUT_SINK Base;
    PS2_KEYBOARD_DRIVER Driver;
} KE_PS2_KEYBOARD_SINK;

HO_KERNEL_API HO_STATUS KePs2KeyboardSinkInit(KE_PS2_KEYBOARD_SINK *sink);
