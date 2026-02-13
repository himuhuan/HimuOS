/**
 * HimuOperatingSystem
 *
 * File: idt.h
 * Description: AMD64 Interrupt Descriptor Table (IDT) definitions.
 * Copyright(c) 2024-2025 HimuOperatingSystem, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "reg.h"

#define IDT_FLAG_INTERRUPT_GATE 0x8E // 64-bit Interrupt Gate (P=1, DPL=0, Type=E)
#define IDT_FLAG_TRAP_GATE      0x8F // 64-bit Trap Gate (P=1, DPL=0, Type=F)

struct IDT_ENTRY
{
    uint16_t OffsetLow;    // Bits 0-15 of handler function address
    uint16_t Selector;     // Code segment selector in GDT or LDT
    uint8_t Ist;           // Bits 0-2: IST index, Bits 3-7: zero
    uint8_t Attributes;    // Type and attributes
    uint16_t OffsetMiddle; // Bits 16-31 of handler function address
    uint32_t OffsetHigh;   // Bits 32-63 of handler function address
    uint32_t Reserved;     // Reserved, set to zero
} __attribute__((packed));

typedef struct IDT_ENTRY IDT_ENTRY;

struct IDT_PTR
{
    uint16_t Limit; // Size of the IDT
    uint64_t Base;  // Base address of the IDT
} __attribute__((packed));

typedef struct KRNL_INTERRUPT_FRAME
{
    X64_GPR Context;
    uint64_t VectorNumber;
    uint64_t ErrorCode;
    uint64_t RIP;
    uint64_t CS;
    uint64_t RFLAGS;
} __attribute__((packed)) KRNL_INTERRUPT_FRAME;

typedef struct USR_INTERRUPT_FRAME
{
    X64_GPR GPR;
    uint64_t VectorNumber;
    uint64_t RIP;
    uint64_t CS;
    uint64_t RFLAGS;
    uint64_t RSP;
    uint64_t SS;
} __attribute__((packed)) USR_INTERRUPT_FRAME;

typedef struct INTERRUPT_FRAME INTERRUPT_FRAME, CPU_DUMP_CONTEXT;

typedef struct IDT_PTR IDT_PTR;

typedef void (*IDT_INTERRUPT_HANDLER)(uint8_t vectorNumber, KRNL_INTERRUPT_FRAME *frame, void *context);

HO_PUBLIC_API void IdtSetEntry(int vn, uint64_t isrAddr, uint16_t selector, uint8_t attributes, uint8_t ist);
HO_PUBLIC_API HO_STATUS IdtInit(void);
HO_PUBLIC_API void IdtDispatchInterrupt(void *frame);
HO_PUBLIC_API HO_STATUS IdtRegisterInterruptHandler(uint8_t vectorNumber, IDT_INTERRUPT_HANDLER handler, void *context);
HO_PUBLIC_API HO_STATUS IdtUnregisterInterruptHandler(uint8_t vectorNumber);
HO_PUBLIC_API const char *IdtGetExceptionMessage(uint8_t vectorNumber);
