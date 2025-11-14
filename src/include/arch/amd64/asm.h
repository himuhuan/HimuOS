/**
 * HimuOperatingSystem
 *
 * File: asm.h
 * Description:
 * Inline assembly functions for amd64 architecture.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

MAYBE_UNUSED static inline uint8_t
inb(uint16_t port)
{
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

MAYBE_UNUSED static inline void
outb(uint16_t port, uint8_t val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

MAYBE_UNUSED static inline void
cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    asm volatile("cpuid"
                 : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) 
                 : "a"(leaf), "c"(0)
    );
}
