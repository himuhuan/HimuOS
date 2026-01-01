#include <kernel/hodbg.h>
#include <kernel/hodefs.h>
#include <kernel/console.h>
#include <arch/amd64/pm.h>
#include <arch/amd64/idt.h> // TODO: remove dependency on x86 arch
#include <arch/arch.h>

#define MAX_STACK_TRACE_DEPTH 20

const char *
KrGetStatusMessage(HO_STATUS status)
{
    // clang-format off
    static const char * kStatusMessages[] = 
    {
        "Operation successful",
        "General failure",
        "Illegal argument",
        "Not enough memory",
        "Should never reach here",
        "Operation not supported",
        "Out of resource",
    };
    // clang-format on
    uint64_t index = (uint64_t)status;
    if (index >= sizeof(kStatusMessages) / sizeof(kStatusMessages[0]))
        return "Unknown error code";
    return kStatusMessages[index];
}

static void
ShowCpuExceptionInfo(int64_t vc, KRNL_INTERRUPT_FRAME *frame)
{
    const char *msg = IdtGetExceptionMessage(vc);
    kprintf(ANSI_BG_BLUE ANSI_FG_WHITE "A fatal error has occurred, and the HimuOS kernel must stop.\n\n");
    kprintf("!!! STOP: CPU Exception %d, EC=%p: %s) !!!\n\n", vc, frame->ErrorCode, msg);
    kprintf("This exception is typically caused by:\n");
    kprintf(" * An internal kernel bug (e.g., invalid memory access, privilege violation).\n");
    kprintf(" * A hardware or VM configuration incompatibility.\n\n");
    kprintf("Restarting the machine may temporarily solve this issue. If this error persists,this indicates a critical "
            "software bug that requires debugging.\n");
    kprintf("If this error persists, please verify your hardware/VM settings against the HimuOS documentation and "
            "report this issue.\n\n");
    kprintf("NOTE: HimuOS has not attempted to modify any disk data. The kernel is now halted.\n\n");
    kprintf("** STOP INFORMATION **\n");
    kprintf("RAX: %p RBX: %p RCX: %p\n", frame->Context.RAX, frame->Context.RBX, frame->Context.RCX);
    kprintf("RDX: %p RSI: %p RDI: %p\n", frame->Context.RDX, frame->Context.RSI, frame->Context.RDI);
    kprintf("RBP: %p R8:  %p R9:  %p\n", frame->Context.RBP, frame->Context.R8, frame->Context.R9);
    kprintf("R10: %p R11: %p R12: %p\n", frame->Context.R10, frame->Context.R11, frame->Context.R12);
    kprintf("R13: %p R14: %p R15: %p\n", frame->Context.R13, frame->Context.R14, frame->Context.R15);
    kprintf("RIP: %p RFL: %p CS:  %p \n", frame->RIP, frame->RFLAGS, frame->CS);
}

static void
PrintStacktrace(uint64_t rbp)
{
    kprintf("\n** STACK TRACE **\n");

    uint64_t *frame_pointer = (uint64_t *)rbp;

    if (rbp == 0)
    {
        kprintf("  (No valid stack frame to trace)\n");
        return;
    }

    for (int i = 0; i < MAX_STACK_TRACE_DEPTH && frame_pointer != NULL; ++i)
    {
        if ((uint64_t)frame_pointer < KRNL_BASE_VA || ((uint64_t)frame_pointer % sizeof(uint64_t)) != 0)
        {
            kprintf("at  %d> Invalid frame pointer: %p\n", i, frame_pointer);
            break;
        }
        uint64_t return_address = frame_pointer[1];
        kprintf("at   %d> %p\n", i, return_address);
        if (return_address == 0)
            break;
        frame_pointer = (uint64_t *)frame_pointer[0];
    }

    kprintf("** END OF STACK TRACE **\n\n");
}

static void
ShowKernelPanicInfo(HO_STATUS code, HO_PANIC_CONTEXT *ctx)
{
    const char *msg = KrGetStatusMessage(code);
    kprintf(ANSI_BG_BLUE ANSI_FG_WHITE "A fatal error has occurred, and the HimuOS kernel must stop.\n\n");
    kprintf("!!! STOP: KERNEL PANIC: %s (%p) !!!\n\n", msg, code);
    kprintf("This error is typically caused by:\n");
    kprintf(" * The kernel heap being exhausted (memory leak or fragmentation).\n");
    kprintf(" * An assertion was failed.\n\n");
    kprintf("Restarting the machine may temporarily solve this issue. If this error persists,this indicates a critical "
            "software bug that requires debugging.\n\n");
    kprintf("NOTE: HimuOS has not attempted to modify any disk data. The kernel is now halted.\n\n");
    kprintf("** STOP INFORMATION **\n");
    if (ctx)
    {
        kprintf("Message:     %s\n", ctx->Message ? ctx->Message : "(null)");
        kprintf("File:        %s\n", ctx->FileName ? ctx->FileName : "(unknown)");
        kprintf("Function:    %s\n", ctx->FunctionName ? ctx->FunctionName : "(unknown)");
        kprintf("Line:        %lu\n", ctx->LineNumber);
        kprintf("RIP:         %p\n", ctx->InstructionPointer);
        kprintf("RBP:         %p\n", ctx->BasePointer);
        PrintStacktrace((uint64_t)ctx->BasePointer);
    }
    else
    {
        kprintf("(No panic context provided)\n");
    }
}

HO_PUBLIC_API HO_NORETURN void
KernelHalt(int64_t ec, void *dump)
{
    ConsoleClearScreen(COLOR_BLUE);

    if (ec <= 0)
    {
        ShowCpuExceptionInfo(-ec, dump);
    }
    else
    {
        ShowKernelPanicInfo(ec, dump);
    }

    Halt();
}
