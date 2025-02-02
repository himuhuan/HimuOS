/**
 * HIMU OPERATING SYSTEM
 *
 * File: i386asm.h
 * Encapsulate some inline assembly code for x86
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#ifndef __HIMUOS__LIB_ASM_I386ASM_H
#define __HIMUOS__LIB_ASM_I386ASM_H

#include "lib/shared/stdint.h"

static inline void outb(uint16_t port, uint8_t data)
{
    asm volatile("outb %b0, %w1" : : "a"(data), "Nd"(port));
}

static inline void outsw(uint16_t port, const void *addr, uint32_t word_cnt)
{
    asm volatile("cld; rep outsw" : "+S"(addr), "+c"(word_cnt) : "d"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t data;
    asm volatile("inb %w1, %b0" : "=a"(data) : "Nd"(port));
    return data;
}

static inline void insw(uint16_t port, void *addr, uint32_t word_cnt)
{
    asm volatile("cld; rep insw" : "+D"(addr), "+c"(word_cnt) : "d"(port) : "memory");
}

#endif // ^^ __HIMUOS__LIB_ASM_I386ASM_H ^^