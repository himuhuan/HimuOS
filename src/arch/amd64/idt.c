#include "arch/amd64/idt.h"
#include "arch/amd64/pm.h"
#include "kernel/hodbg.h"
#include "libc/string.h"

#define IDT_EXCEPTION_VECTOR_COUNT 32
#define IDT_TOTAL_VECTOR_COUNT     256
#define IDT_EXCEPTION_GATE         IDT_FLAG_TRAP_GATE
#define IDT_EXTERNAL_GATE          IDT_FLAG_INTERRUPT_GATE
#define IDT_SPURIOUS_VECTOR        0xFF

static IDT_ENTRY kInterruptDescriptorTable[IDT_TOTAL_VECTOR_COUNT];
static IDT_PTR kIdtPtr;
static IDT_INTERRUPT_HANDLER kInterruptHandlers[IDT_TOTAL_VECTOR_COUNT];
static void *kInterruptHandlerContexts[IDT_TOTAL_VECTOR_COUNT];
extern void *gIsrStubTable[];

static inline void
LoadIdt(IDT_PTR *pIdtPtr)
{
    __asm__ __volatile__("lidt %0" : : "m"(*pIdtPtr));
}

static HO_NORETURN void
PanicUnhandledExternalInterrupt(uint8_t vectorNumber, KRNL_INTERRUPT_FRAME *frame)
{
    kprintf("FATAL: Unhandled external interrupt vector %u at RIP=%p\n", vectorNumber, (void *)frame->RIP);
    HO_KPANIC(EC_INVALID_STATE, "Unhandled external interrupt");
}

HO_PUBLIC_API void
IdtSetEntry(int vectorNumber, uint64_t isrAddr, uint16_t selector, uint8_t attributes, uint8_t ist)
{
    IDT_ENTRY *entry = &kInterruptDescriptorTable[vectorNumber];
    entry->OffsetLow = (uint16_t)(isrAddr & 0xFFFF);
    entry->Selector = selector;
    entry->Ist = ist & 0x7;
    entry->Attributes = attributes;
    entry->OffsetMiddle = (uint16_t)((isrAddr >> 16) & 0xFFFF);
    entry->OffsetHigh = (uint32_t)((isrAddr >> 32) & 0xFFFFFFFF);
    entry->Reserved = 0;
}

HO_PUBLIC_API void
IdtDispatchInterrupt(void *frame)
{
    KRNL_INTERRUPT_FRAME *dump = (KRNL_INTERRUPT_FRAME *)frame;
    uint8_t vectorNumber = (uint8_t)dump->VectorNumber;

    if (vectorNumber < IDT_EXCEPTION_VECTOR_COUNT)
    {
        KernelHalt(-((int64_t)vectorNumber), dump);
    }

    if (vectorNumber == IDT_SPURIOUS_VECTOR)
    {
        return;
    }

    IDT_INTERRUPT_HANDLER handler = kInterruptHandlers[vectorNumber];
    if (handler == NULL)
    {
        PanicUnhandledExternalInterrupt(vectorNumber, dump);
    }

    handler(vectorNumber, dump, kInterruptHandlerContexts[vectorNumber]);
}

HO_PUBLIC_API HO_STATUS
IdtRegisterInterruptHandler(uint8_t vectorNumber, IDT_INTERRUPT_HANDLER handler, void *context)
{
    if (vectorNumber < IDT_EXCEPTION_VECTOR_COUNT || handler == NULL)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    if (kInterruptHandlers[vectorNumber] != NULL)
    {
        return EC_INVALID_STATE;
    }

    kInterruptHandlers[vectorNumber] = handler;
    kInterruptHandlerContexts[vectorNumber] = context;
    return EC_SUCCESS;
}

HO_PUBLIC_API HO_STATUS
IdtUnregisterInterruptHandler(uint8_t vectorNumber)
{
    if (vectorNumber < IDT_EXCEPTION_VECTOR_COUNT)
    {
        return EC_ILLEGAL_ARGUMENT;
    }

    kInterruptHandlers[vectorNumber] = NULL;
    kInterruptHandlerContexts[vectorNumber] = NULL;
    return EC_SUCCESS;
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
IdtInit(void)
{
    memset(kInterruptDescriptorTable, 0, sizeof(kInterruptDescriptorTable));
    memset(kInterruptHandlers, 0, sizeof(kInterruptHandlers));
    memset(kInterruptHandlerContexts, 0, sizeof(kInterruptHandlerContexts));

    for (int i = 0; i < IDT_EXCEPTION_VECTOR_COUNT; i++)
    {
        IdtSetEntry(i, (uint64_t)gIsrStubTable[i], GDT_KRNL_CODE_SEL, IDT_EXCEPTION_GATE, 1);
    }

    for (int i = IDT_EXCEPTION_VECTOR_COUNT; i < IDT_TOTAL_VECTOR_COUNT; i++)
    {
        IdtSetEntry(i, (uint64_t)gIsrStubTable[i], GDT_KRNL_CODE_SEL, IDT_EXTERNAL_GATE, 0);
    }

    kIdtPtr.Limit = sizeof(kInterruptDescriptorTable) - 1;
    kIdtPtr.Base = (uint64_t)&kInterruptDescriptorTable;

    LoadIdt(&kIdtPtr);
    return EC_SUCCESS;
}
