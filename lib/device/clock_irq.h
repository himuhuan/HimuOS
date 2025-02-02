/**
 * HIMU OPERATING SYSTEM
 *
 * File: clock_irq.h
 * The clock interrupt
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS_LIB_DEVICE_CLOCK_IRQ_H
#define __HIMUOS_LIB_DEVICE_CLOCK_IRQ_H

#include "lib/shared/stdint.h"

/* Set the Clock Irq frequency */
void SetClockIrq(void);

void InitClockIrq(void);

#endif // ^^ __HIMUOS_LIB_DEVICE_CLOCK_IRQ_H ^^
