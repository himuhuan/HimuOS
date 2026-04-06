/**
 * HimuOperatingSystem
 *
 * File: ke/input/sinks/ps2_keyboard_sink.c
 * Description: Ke input sink adapter for the minimal PS/2 keyboard driver.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "ps2_keyboard_sink.h"

static HO_STATUS
Ps2KeyboardSinkInit(void *self)
{
    KE_PS2_KEYBOARD_SINK *sink = (KE_PS2_KEYBOARD_SINK *)self;
    return Ps2KeyboardDriverInit(&sink->Driver);
}

static BOOL
Ps2KeyboardSinkHasPendingData(void *self)
{
    KE_PS2_KEYBOARD_SINK *sink = (KE_PS2_KEYBOARD_SINK *)self;
    return Ps2KeyboardDriverHasPendingData(&sink->Driver);
}

static HO_STATUS
Ps2KeyboardSinkReadScanCode(void *self, uint8_t *outScanCode)
{
    KE_PS2_KEYBOARD_SINK *sink = (KE_PS2_KEYBOARD_SINK *)self;
    return Ps2KeyboardDriverReadScanCode(&sink->Driver, outScanCode);
}

static void
Ps2KeyboardSinkAcknowledgeInterrupt(void *self)
{
    KE_PS2_KEYBOARD_SINK *sink = (KE_PS2_KEYBOARD_SINK *)self;
    Ps2KeyboardDriverAcknowledgeInterrupt(&sink->Driver);
}

static const char *
Ps2KeyboardSinkGetName(MAYBE_UNUSED void *self)
{
    return "qemu-ps2-keyboard";
}

HO_KERNEL_API HO_STATUS
KePs2KeyboardSinkInit(KE_PS2_KEYBOARD_SINK *sink)
{
    if (sink == NULL)
        return EC_ILLEGAL_ARGUMENT;

    sink->Base.Init = Ps2KeyboardSinkInit;
    sink->Base.HasPendingData = Ps2KeyboardSinkHasPendingData;
    sink->Base.ReadScanCode = Ps2KeyboardSinkReadScanCode;
    sink->Base.AcknowledgeInterrupt = Ps2KeyboardSinkAcknowledgeInterrupt;
    sink->Base.GetName = Ps2KeyboardSinkGetName;
    memset(&sink->Driver, 0, sizeof(sink->Driver));
    return EC_SUCCESS;
}
