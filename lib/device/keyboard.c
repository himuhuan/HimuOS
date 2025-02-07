/**
 * HIMU OPERATING SYSTEM
 *
 * File: keyboard.c
 * Keyboard device
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */
#include "krnltypes.h"
#include "keyboard.h"
#include "iocbuf.h"
#include "interrupt.h"
#include "lib/kernel/krnlio.h"
#include "lib/asm/i386asm.h"

#define KBD_BUFFER_PORT 0x60

///////////////////////////////////////////////////////////////////////////////////////////////////
// scancodes
///////////////////////////////////////////////////////////////////////////////////////////////////

#define ESC             '\033'
#define BACKSPACE       '\b'
#define TAB             '\t'
#define ENTER           '\r'
#define DELETE          '\177'

#define CHAR_INVISIBLE  0
#define CTRL_L_CHAR     CHAR_INVISIBLE
#define CTRL_R_CHAR     CHAR_INVISIBLE
#define SHIFT_L_CHAR    CHAR_INVISIBLE
#define SHIFT_R_CHAR    CHAR_INVISIBLE
#define ALT_L_CHAR      CHAR_INVISIBLE
#define ALT_R_CHAR      CHAR_INVISIBLE
#define CAPS_LOCK_CHAR  CHAR_INVISIBLE
#define SHIFT_L_MAKE    0x2a
#define SHIFT_R_MAKE    0x36
#define ALT_L_MAKE      0x38
#define ALT_R_MAKE      0xe038
#define ALT_R_BREAK     0xe0b8
#define CTRL_L_MAKE     0x1d
#define CTRL_R_MAKE     0xe01d
#define CTRL_R_BREAK    0xe09d
#define CAPS_LOCK_MAKE  0x3a

struct IO_CIR_BUFFER gKeyboardBuffer;

static BOOL kCtrlStatus, kShiftStatus, kAltStatus, kCapsLockStatus, kExtScancode;

static char kKeymap[][2] = {
    /* 0x00 */ {0, 0},
    /* 0x01 */ {ESC, ESC},
    /* 0x02 */ {'1', '!'},
    /* 0x03 */ {'2', '@'},
    /* 0x04 */ {'3', '#'},
    /* 0x05 */ {'4', '$'},
    /* 0x06 */ {'5', '%'},
    /* 0x07 */ {'6', '^'},
    /* 0x08 */ {'7', '&'},
    /* 0x09 */ {'8', '*'},
    /* 0x0A */ {'9', '('},
    /* 0x0B */ {'0', ')'},
    /* 0x0C */ {'-', '_'},
    /* 0x0D */ {'=', '+'},
    /* 0x0E */ {BACKSPACE, BACKSPACE},
    /* 0x0F */ {TAB, TAB},
    /* 0x10 */ {'q', 'Q'},
    /* 0x11 */ {'w', 'W'},
    /* 0x12 */ {'e', 'E'},
    /* 0x13 */ {'r', 'R'},
    /* 0x14 */ {'t', 'T'},
    /* 0x15 */ {'y', 'Y'},
    /* 0x16 */ {'u', 'U'},
    /* 0x17 */ {'i', 'I'},
    /* 0x18 */ {'o', 'O'},
    /* 0x19 */ {'p', 'P'},
    /* 0x1A */ {'[', '{'},
    /* 0x1B */ {']', '}'},
    /* 0x1C */ {ENTER, ENTER},
    /* 0x1D */ {CTRL_L_CHAR, CTRL_L_CHAR},
    /* 0x1E */ {'a', 'A'},
    /* 0x1F */ {'s', 'S'},
    /* 0x20 */ {'d', 'D'},
    /* 0x21 */ {'f', 'F'},
    /* 0x22 */ {'g', 'G'},
    /* 0x23 */ {'h', 'H'},
    /* 0x24 */ {'j', 'J'},
    /* 0x25 */ {'k', 'K'},
    /* 0x26 */ {'l', 'L'},
    /* 0x27 */ {';', ':'},
    /* 0x28 */ {'\'', '"'},
    /* 0x29 */ {'`', '~'},
    /* 0x2A */ {SHIFT_L_CHAR, SHIFT_L_CHAR},
    /* 0x2B */ {'\\', '|'},
    /* 0x2C */ {'z', 'Z'},
    /* 0x2D */ {'x', 'X'},
    /* 0x2E */ {'c', 'C'},
    /* 0x2F */ {'v', 'V'},
    /* 0x30 */ {'b', 'B'},
    /* 0x31 */ {'n', 'N'},
    /* 0x32 */ {'m', 'M'},
    /* 0x33 */ {',', '<'},
    /* 0x34 */ {'.', '>'},
    /* 0x35 */ {'/', '?'},
    /* 0x36 */ {SHIFT_R_CHAR, SHIFT_R_CHAR},
    /* 0x37 */ {'*', '*'},
    /* 0x38 */ {ALT_L_CHAR, ALT_L_CHAR},
    /* 0x39 */ {' ', ' '},
    /* 0x3A */ {CAPS_LOCK_CHAR, CAPS_LOCK_CHAR}};

#define KEYMAP_LENGTH (sizeof(kKeymap) / sizeof(kKeymap[0]))

static void IntrKeyboardHandler(void) {
    BOOL /*ctrlDown, */ shiftDown, capsDown, breakcode, shift;
    uint16_t            scancode;
    char                ch;

    // ctrlDown  = kCtrlStatus;
    shiftDown = kShiftStatus;
    capsDown  = kCapsLockStatus;
    scancode  = inb(KBD_BUFFER_PORT);

    /* extend code */
    if (scancode == 0xe0) {
        kExtScancode = TRUE;
        return;
    }
    if (kExtScancode) {
        scancode |= 0xe0000;
        kExtScancode = FALSE;
    }

    breakcode = ((scancode & 0x0080) != 0);
    if (breakcode) /* break code */ {
        scancode &= 0xff7f;
        /* key released */
        if (scancode == CTRL_L_MAKE || scancode == CTRL_R_MAKE)
            kCtrlStatus = FALSE;
        else if (scancode == SHIFT_L_MAKE || scancode == SHIFT_R_MAKE)
            kShiftStatus = FALSE;
        else if (scancode == ALT_L_MAKE || scancode == ALT_R_MAKE)
            kAltStatus = FALSE;
    } else if ((scancode > 0 && scancode < KEYMAP_LENGTH) || scancode == ALT_R_MAKE || scancode == CTRL_R_MAKE) {
        shift = FALSE;
        // clang-format off
        if ((scancode < 0x0e) || (scancode == 0x29) || (scancode == 0x1a) 
            || (scancode == 0x1b) || (scancode == 0x2b) || (scancode == 0x27) 
            || (scancode == 0x28) || (scancode == 0x33) || (scancode == 0x34) || (scancode == 0x35)) 
        {
            shift = shiftDown;
        }
        else /* alpha */ 
        {
            if (shiftDown && capsDown) shift = FALSE;
            else if (shiftDown || capsDown) shift = TRUE;
            else shift = FALSE;
        }
        // clang-format on

        scancode &= 0x00ff;
        ch = kKeymap[scancode][shift];
        if (ch) {
            if (!IcbFull(&gKeyboardBuffer)) {
                // PrintChar(ch);
                IcbPut(&gKeyboardBuffer, ch);
            }
            return;
        }

        switch (scancode) {
        case SHIFT_L_MAKE:
        case SHIFT_R_MAKE:
            kShiftStatus = TRUE;
            break;
        case CTRL_L_MAKE:
        case CTRL_R_MAKE:
            kCtrlStatus = TRUE;
            break;
        case ALT_L_MAKE:
        case ALT_R_MAKE:
            kAltStatus = TRUE;
            break;
        case CAPS_LOCK_MAKE:
            kCapsLockStatus = !kCapsLockStatus;
            break;
        }
    } else {
#ifndef __HIMUOS_RELEASE__
        PrintStr("Unkown key: ");
        PrintHex(scancode);
        PrintChar('\n');
#endif //! __HIMUOS_RELEASE__
    }
}

void InitKeyboard(void) {
    PrintStr("InitKeyboard START\n");
    IcbInit(&gKeyboardBuffer);
    KrRegisterIntrHandler(0x21, IntrKeyboardHandler);
    PrintStr("InitKeyboard END\n");
}