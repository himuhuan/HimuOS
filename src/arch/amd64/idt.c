#include "arch/amd64/idt.h"
#include "arch/amd64/pm.h"
#include "kernel/hodbg.h"
#include <libc/string.h>

static IDT_ENTRY kInterruptDescriptorTable[256];
static IDT_PTR kIdtPtr;

typedef struct IDT_IRQ_HANDLER_ENTRY
{
    IDT_INTERRUPT_HANDLER Handler;
    void *Context;
} IDT_IRQ_HANDLER_ENTRY;

static IDT_IRQ_HANDLER_ENTRY kInterruptHandlers[256];

extern void *gIsrStubTable[];

static inline void
LoadIdt(IDT_PTR *pIdtPtr)
{
    __asm__ __volatile__("lidt %0" : : "m"(*pIdtPtr));
}

static void
HandleExternalInterrupt(KRNL_INTERRUPT_FRAME *frame)
{
    uint8_t vectorNumber = (uint8_t)frame->VectorNumber;
    IDT_IRQ_HANDLER_ENTRY *entry = &kInterruptHandlers[vectorNumber];

    if (entry->Handler == NULL)
    {
        klog(KLOG_LEVEL_ERROR, "[IDT] Unhandled external interrupt vector=%u\n", vectorNumber);
        return;
    }

    entry->Handler(frame, entry->Context);
}

void
IdtSetEntry(int vn, uint64_t isrAddr, uint16_t selector, uint8_t attributes, uint8_t ist)
{
    IDT_ENTRY *entry = &kInterruptDescriptorTable[vn];
    entry->OffsetLow = (uint16_t)(isrAddr & 0xFFFF);
    entry->Selector = selector;
    entry->Ist = ist & 0x7;
    entry->Attributes = attributes;
    entry->OffsetMiddle = (uint16_t)((isrAddr >> 16) & 0xFFFF);
    entry->OffsetHigh = (uint32_t)((isrAddr >> 32) & 0xFFFFFFFF);
    entry->Reserved = 0;
}

HO_PUBLIC_API void
IdtExceptionHandler(void *frame)
{
    KRNL_INTERRUPT_FRAME *dump = (KRNL_INTERRUPT_FRAME *)frame;
    uint8_t vectorNumber = (uint8_t)dump->VectorNumber;

    if (vectorNumber < 32)
    {
        KernelHalt(-(int64_t)vectorNumber, dump);
        return;
    }

    HandleExternalInterrupt(dump);
}

HO_PUBLIC_API const char *
IdtGetExceptionMessage(uint8_t vectorNumber)
{
    // clang-format off
    static const char *kExceptionMessages[] = 
    {
        /*  0 */ "#DE Divide Error",
        /*  1 */ "#DB Debug",
        /*  2 */ "NMI Interrupt",
        /*  3 */ "#BP Breakpoint",
        /*  4 */ "#OF Overflow",
        /*  5 */ "#BR Bound Range Exceeded",
        /*  6 */ "#UD Invalid Opcode",
        /*  7 */ "#NM Device Not Available",
        /*  8 */ "#DF Double Fault",
        /*  9 */ "Coprocessor Segment Overrun",
        /* 10 */ "#TS Invalid TSS",
        /* 11 */ "#NP Segment Not Present",
        /* 12 */ "#SS Stack-Segment Fault",
        /* 13 */ "#GP General Protection Fault",
        /* 14 */ "#PF Page Fault",
        /* 15 */ "Reserved",
        /* 16 */ "#MF x87 Floating-Point Exception",
        /* 17 */ "#AC Alignment Check",
        /* 18 */ "#MC Machine Check",
        /* 19 */ "#XM SIMD Floating-Point Exception",
        /* 20 */ "#VE Virtualization Exception",
        /* 21 */ "#CP Control Protection Exception",
        /* 22 */ "Reserved",
        /* 23 */ "Reserved",
        /* 24 */ "Reserved",
        /* 25 */ "Reserved",
        /* 26 */ "Reserved",
        /* 27 */ "Reserved",
        /* 28 */ "#HV Hypervisor Injection Exception",
        /* 29 */ "#VC VMM Communication Exception",
        /* 30 */ "#SX Security Exception",
        /* 31 */ "Reserved"
    };
    // clang-format on

    if (HO_LIKELY(vectorNumber < sizeof(kExceptionMessages) / sizeof(kExceptionMessages[0])))
        return kExceptionMessages[vectorNumber];

    return "Unknown Exception";
}

HO_PUBLIC_API HO_STATUS
IdtRegisterInterruptHandler(uint8_t vectorNumber, IDT_INTERRUPT_HANDLER handler, void *context)
{
    if (vectorNumber < 32 || handler == NULL)
        return EC_ILLEGAL_ARGUMENT;

    kInterruptHandlers[vectorNumber].Handler = handler;
    kInterruptHandlers[vectorNumber].Context = context;
    return EC_SUCCESS;
}

HO_PUBLIC_API HO_STATUS
IdtInit(void)
{
    memset(kInterruptHandlers, 0, sizeof(kInterruptHandlers));

    // All vectors use IST1 currently. Exceptions are trap gates, external vectors are interrupt gates.
    for (int i = 0; i < 256; i++)
    {
        uint8_t attributes = (i < 32) ? IDT_FLAG_TRAP_GATE : IDT_FLAG_INTERRUPT_GATE;
        IdtSetEntry(i, (uint64_t)gIsrStubTable[i], GDT_KRNL_CODE_SEL, attributes, 1);
    }

    kIdtPtr.Limit = sizeof(kInterruptDescriptorTable) - 1;
    kIdtPtr.Base = (uint64_t)&kInterruptDescriptorTable;

    LoadIdt(&kIdtPtr);
    return EC_SUCCESS;
}
