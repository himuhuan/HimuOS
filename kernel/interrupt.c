/**
 * HIMU OPERATING SYSTEM
 *
 * File: interruptr.c
 * Installation of IDT and Interrupt Handler
 * Copyright (C) 2024 HimuOS Project, all rights reserved.
 */

#include "interrupt.h"

#include "lib/asm/i386asm.h"
#include "lib/asm/i386def.h"
#include "lib/kernel/krnlio.h"
#include "lib/shared/stdint.h"

#define PIC_M_CTRL   0x20
#define PIC_M_DATA   0x21
#define PIC_S_CTRL   0xa0
#define PIC_S_DATA   0xa1

#define IDT_DESC_CNT 33

#define EFLAGS_IF    0x00000200

struct GateDesc {
    uint16_t OffsetLowWord;
    uint16_t Selector;
    uint8_t  DCount;
    uint8_t  Attribute;
    uint16_t OffsetHighWord;
};

static void            MakeIdtDesc(struct GateDesc *p_gdesc, uint8_t attr, IntrHandler function);
static struct GateDesc idt[IDT_DESC_CNT];

/* Kernel.S 中的处理程序只是入口程序 */
extern IntrHandler IntrEntryTable[IDT_DESC_CNT];
/* 中断名称 */
const char *IntrName[IDT_DESC_CNT];
/* IntrEntryTable 中的函数将会重定向到实际处理事件的函数 */
IntrHandler IntrHandlerTable[IDT_DESC_CNT];

/* 对于中断的默认处理程序 */
static void DefaultIntrHandler(int vectorNumber) {
    // spurious interrupt
    if (vectorNumber == 0x27 || vectorNumber == 0x2f)
        return;

    ClearScreen();
    PrintStr("------------- !!UNEXPECTED INTERRUPT!! ----------------\n");
    PrintStr("  INTR (");
    PrintAddr((void *)vectorNumber);
    PrintStr(") : ");
    if (IntrName[vectorNumber] != 0)
        PrintStr(IntrName[vectorNumber]);
    if (vectorNumber == 14) { // page fault
        uint32_t pageFaultAddr;
        asm("movl %%cr2, %0" : "=r"(pageFaultAddr));
        PrintStr("\n  at: ");
        PrintAddr((void *)pageFaultAddr);
    }
    PrintStr("\n-------------------------------------------------------\n");

    asm volatile("hlt");
}

static void PicInit(void) {
    outb(PIC_M_CTRL, 0x11);
    outb(PIC_M_DATA, 0x20);
    outb(PIC_M_DATA, 0x04);
    outb(PIC_M_DATA, 0x01);
    outb(PIC_S_CTRL, 0x11);
    outb(PIC_S_DATA, 0x28);
    outb(PIC_S_DATA, 0x02);
    outb(PIC_S_DATA, 0x01);
    outb(PIC_M_DATA, 0xfe);
    outb(PIC_S_DATA, 0xff);
}

/* 创建中断门描述符 */
static void MakeIdtDesc(struct GateDesc *p_gdesc, uint8_t attr, IntrHandler function) {
    p_gdesc->OffsetLowWord  = (uint32_t)function & 0x0000FFFF;
    p_gdesc->Selector       = SELECTOR_K_CODE;
    p_gdesc->DCount         = 0;
    p_gdesc->Attribute      = attr;
    p_gdesc->OffsetHighWord = ((uint32_t)function & 0xFFFF0000) >> 16;
}

/*初始化中断描述符表*/
static void IdtDescInit(void) {
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        MakeIdtDesc(&idt[i], IDT_DESC_ATTR_DPL0, IntrEntryTable[i]);
    }
}

static void IntrHandlerInit(void) {
    int i;
    for (i = 0; i < IDT_DESC_CNT; i++) {
        IntrHandlerTable[i] = DefaultIntrHandler;
        IntrName[i]         = "Unknown";
    }
    IntrName[0]  = "#DE Divide Error";
    IntrName[1]  = "#DB Debug Exception";
    IntrName[2]  = "NMI Interrupt";
    IntrName[3]  = "#BP Breakpoint Exception";
    IntrName[4]  = "#OF Overflow Exception";
    IntrName[5]  = "#BR BOUND Range Exceeded Exception";
    IntrName[6]  = "#UD Invalid Opcode Exception";
    IntrName[7]  = "#NM Device Not Available Exception";
    IntrName[8]  = "#DF Double Fault Exception";
    IntrName[9]  = "Coprocessor Segment Overrun";
    IntrName[10] = "#TS Invalid TSS Exception";
    IntrName[11] = "#NP Segment Not Present";
    IntrName[12] = "#SS Stack Fault Exception";
    IntrName[13] = "#GP General Protection Exception";
    IntrName[14] = "#PF Page-Fault Exception";
    // IntrName[15] = "#15 (Intel reserved. Do not use.)";
    IntrName[16] = "#MF x87 FPU Floating-Point Error";
    IntrName[17] = "#AC Alignment Check Exception";
    IntrName[18] = "#MC Machine-Check Exception";
    IntrName[19] = "#XF SIMD Floating-Point Exception";
    IntrName[32] = "Timer Interrupt";
}

void InitIdt() {
    PrintStr("InitIdt...");
    IdtDescInit();
    IntrHandlerInit();
    PicInit();
    uint64_t idtOperand = ((sizeof(idt) - 1) | ((uint64_t)(uint32_t)idt << 16));
    asm volatile("lidt %0" : : "m"(idtOperand));
    PrintStr("OK\n");
}

uint8_t GetIntrStatus(void) {
    uint32_t eflags;

    eflags = 0;
    asm volatile("pushfl; popl %0" : "=g"(eflags));
    return (EFLAGS_IF & eflags) ? INTR_STATUS_ON : INTR_STATUS_OFF;
}

uint8_t SetIntrStatus(uint16_t status) { return (status & INTR_STATUS_ON) ? EnableIntr() : DisableIntr(); }

uint8_t DisableIntr(void) {
    uint8_t oldStatus;

    oldStatus = GetIntrStatus();
    if (oldStatus == INTR_STATUS_ON) {
        // PrintStr("DisableIntr\n");
        asm volatile("cli" : : : "memory");
    }
    return oldStatus;
}

uint8_t EnableIntr(void) {
    uint8_t oldStatus;

    oldStatus = GetIntrStatus();
    if (oldStatus == INTR_STATUS_OFF) {
        // PrintStr("EnableIntr\n");
        asm volatile("sti");
    }
    return oldStatus;
}

void KrRegisterIntrHandler(uint8_t vectorNo, IntrHandler handler) {
    PrintStr("-> Handler ");
    PrintAddr(handler);
    PrintStr(" installed for interrupt ");
    PrintInt(vectorNo);
    PrintChar('\n');

    IntrHandlerTable[vectorNo] = handler;
}