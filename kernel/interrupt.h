/**
 * HIMU OPERATING SYSTEM
 *
 * File: interruptr.h
 * Basic Definition For IDT and Interrupt Handler
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS__KERNEL_INTERRUPT_H
#define __HIMUOS__KERNEL_INTERRUPT_H

#include "lib/shared/stdint.h"

typedef void *IntrHandler;

/* Initialize IDT */
void InitIdt(void);

#define INTR_STATUS_OFF 0
#define INTR_STATUS_ON  1

uint8_t GetIntrStatus(void);

uint8_t SetIntrStatus(uint16_t status);

uint8_t DisableIntr(void);

uint8_t EnableIntr(void);

void KrRegisterIntrHandler(uint8_t vectorNo, IntrHandler handler);

#endif //^^ __HIMUOS__KERNEL_KRNLIO_H ^^
