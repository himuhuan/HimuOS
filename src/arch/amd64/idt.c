#include "arch/amd64/idt.h"
#include "arch/amd64/pm.h"
#include "kernel/hodbg.h"
#include <kernel/init.h>
#include <kernel/ke/irql.h>
#include <kernel/ke/user_bootstrap.h>
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

static uint8_t GetVectorGateType(uint8_t vectorNumber);
static uint8_t GetVectorIstIndex(uint8_t vectorNumber);
static BOOL IsSynchronousTrapVector(uint8_t vectorNumber);

static inline HO_VIRTUAL_ADDRESS
ReadCr2(void)
{
    uint64_t cr2;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    return (HO_VIRTUAL_ADDRESS)cr2;
}

static BOOL
IsCurrentPageFaultDiagnosticContext(void)
{
    BOOT_CAPSULE *capsule = KeGetBootCapsule();
    if (!capsule || capsule->Layout.IST2StackSize == 0 || capsule->CpuInfo.Tss.IST2 == 0)
        return FALSE;

    uint8_t stackProbe;
    HO_VIRTUAL_ADDRESS currentSp = (HO_VIRTUAL_ADDRESS)(uint64_t)&stackProbe;
    HO_VIRTUAL_ADDRESS ist2Base = capsule->CpuInfo.Tss.IST2 - capsule->Layout.IST2StackSize;
    return currentSp >= ist2Base && currentSp < capsule->CpuInfo.Tss.IST2;
}

static inline void
LoadIdt(IDT_PTR *pIdtPtr)
{
    __asm__ __volatile__("lidt %0" : : "m"(*pIdtPtr));
}

static void
HandleRegisteredVector(INTERRUPT_FRAME *frame)
{
    uint8_t vectorNumber = (uint8_t)frame->VectorNumber;
    IDT_IRQ_HANDLER_ENTRY *entry = &kInterruptHandlers[vectorNumber];

    if (entry->Handler == NULL)
    {
        klog(KLOG_LEVEL_ERROR, "[IDT] Unhandled registered vector=%u\n", vectorNumber);
        return;
    }

    entry->Handler(frame, entry->Context);
}

static uint8_t
GetVectorGateType(uint8_t vectorNumber)
{
    switch (vectorNumber)
    {
    case KE_USER_BOOTSTRAP_SYSCALL_VECTOR:
        return IDT_FLAG_USER_TRAP_GATE;
    case 3: // #BP Breakpoint
    case 4: // #OF Overflow
        return IDT_FLAG_TRAP_GATE;
    default:
        return IDT_FLAG_INTERRUPT_GATE;
    }
}

static uint8_t
GetVectorIstIndex(uint8_t vectorNumber)
{
    switch (vectorNumber)
    {
    case 8: // #DF Double Fault
        return 1;
    case 14: // #PF Page Fault
        return 2;
    default:
        return 0;
    }
}

static BOOL
IsSynchronousTrapVector(uint8_t vectorNumber)
{
    return vectorNumber == KE_USER_BOOTSTRAP_SYSCALL_VECTOR;
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
    INTERRUPT_FRAME *dump = (INTERRUPT_FRAME *)frame;
    uint8_t vectorNumber = (uint8_t)dump->VectorNumber;

    if (vectorNumber < 32)
    {
        HO_CPU_EXCEPTION_CONTEXT context;
        memset(&context, 0, sizeof(context));
        context.Frame = dump;

        if (vectorNumber == 14) // #PF
        {
            context.HasFaultAddress = TRUE;
            context.FaultAddress = ReadCr2();
            context.PageFaultErrorCode = (uint32_t)dump->ErrorCode;
            context.IsSafePageFaultContext = IsCurrentPageFaultDiagnosticContext();
        }

        KernelHalt(-(int64_t)vectorNumber, &context);
        return;
    }

    if (IsSynchronousTrapVector(vectorNumber))
    {
        HandleRegisteredVector(dump);
        return;
    }

    KeEnterInterruptContext();
    HandleRegisteredVector(dump);
    KeLeaveInterruptContext();
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

    for (int i = 0; i < 256; i++)
    {
        uint8_t vectorNumber = (uint8_t)i;
        uint8_t attributes = GetVectorGateType(vectorNumber);
        uint8_t istIndex = GetVectorIstIndex(vectorNumber);
        IdtSetEntry(i, (uint64_t)gIsrStubTable[i], GDT_KRNL_CODE_SEL, attributes, istIndex);
    }

    kIdtPtr.Limit = sizeof(kInterruptDescriptorTable) - 1;
    kIdtPtr.Base = (uint64_t)&kInterruptDescriptorTable;

    LoadIdt(&kIdtPtr);
    return EC_SUCCESS;
}
