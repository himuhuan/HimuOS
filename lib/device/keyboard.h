/**
 * HIMU OPERATING SYSTEM
 *
 * File: keyboard.h
 * Keyboard device
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS_LIB_DEVICE_KEYBOARD_H
#define __HIMUOS_LIB_DEVICE_KEYBOARD_H 1

#include "iocbuf.h"

void InitKeyboard(void);

extern struct IO_CIR_BUFFER gKeyboardBuffer;

#endif //! __HIMUOS_LIB_DEVICE_KEYBOARD_H