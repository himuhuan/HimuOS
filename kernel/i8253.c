/**
 * HIMU OPERATING SYSTEM
 *
 * File: i8253.c
 * Implementation of the i8253 timer for clock irq
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "lib/asm/i386asm.h"
#include "lib/device/clock_irq.h"
#include "lib/kernel/krnlio.h"

#define INPUT_FREQUENCY  1193180
#define CONTRER0_PORT    0x40
#define COUNTER0_NO      0
#define IRQ0_HZ          100
#define IRQ0_FREQUENCY   (INPUT_FREQUENCY / IRQ0_HZ)
#define COUNTER_MODE     2
#define READ_WRITE_LATCH 3
#define PIT_CONTROL_PORT 0x43

void SetClockIrq()
{
    PrintStr("SetClockIrq...");
    outb(PIT_CONTROL_PORT, (uint8_t)(COUNTER0_NO << 6 | READ_WRITE_LATCH << 4 | COUNTER_MODE << 1));
    outb(CONTRER0_PORT, (uint8_t)IRQ0_FREQUENCY);
    outb(CONTRER0_PORT, (uint8_t)(IRQ0_FREQUENCY >> 8));
    PrintStr("DONE\n");
}
