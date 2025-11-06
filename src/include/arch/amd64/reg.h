/**
 * HimuOperatingSystem
 *
 * File: reg.h
 * Description:
 * Registers & CPU context definitions.
 *
 * Only for AMD64 architecture.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

// CPU General Purpose Registers (GPR) context structure
// Sorted in the order of PUSH operations in the ISR prologue
typedef struct X64_GPR
{
    uint64_t R15;
    uint64_t R14;
    uint64_t R13;
    uint64_t R12;
    uint64_t R11;
    uint64_t R10;
    uint64_t R9;
    uint64_t R8;
    uint64_t RDI;
    uint64_t RSI;
    uint64_t RBP;
    uint64_t RBX;
    uint64_t RDX;
    uint64_t RCX;
    uint64_t RAX;
} __attribute__((packed)) X64_GPR;
