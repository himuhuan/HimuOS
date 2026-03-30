#include <kernel/hodbg.h>
#include <kernel/hodefs.h>
#include <kernel/ke/console.h>
#include <kernel/ke/mm.h>
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
        "Operation cannot be performed in the current state",
        "The machine architecture is not supported",
        "Wait timed out before being satisfied"
    };
    // clang-format on
    uint64_t index = (uint64_t)status;
    if (index >= sizeof(kStatusMessages) / sizeof(kStatusMessages[0]))
        return "Unknown error code";
    return kStatusMessages[index];
}

static void
PrintPageFaultErrorBits(uint32_t errorCode)
{
    kprintf("PFERR: P=%u W=%u U=%u RSVD=%u I=%u PK=%u SS=%u SGX=%u\n", errorCode & 0x1, (errorCode >> 1) & 0x1,
            (errorCode >> 2) & 0x1, (errorCode >> 3) & 0x1, (errorCode >> 4) & 0x1, (errorCode >> 5) & 0x1,
            (errorCode >> 6) & 0x1, (errorCode >> 15) & 0x1);
}

static const char *
VmmRegionTypeName(uint16_t type)
{
    switch (type)
    {
    case BOOT_MAPPING_REGION_IDENTITY:
        return "identity";
    case BOOT_MAPPING_REGION_HHDM:
        return "hhdm";
    case BOOT_MAPPING_REGION_BOOT_STAGING:
        return "boot_staging";
    case BOOT_MAPPING_REGION_BOOT_HANDOFF:
        return "boot_handoff";
    case BOOT_MAPPING_REGION_BOOT_PAGE_TABLES:
        return "boot_page_tables";
    case BOOT_MAPPING_REGION_ACPI_RSDP:
        return "acpi_rsdp";
    case BOOT_MAPPING_REGION_ACPI_ROOT:
        return "acpi_root";
    case BOOT_MAPPING_REGION_ACPI_TABLE:
        return "acpi_table";
    case BOOT_MAPPING_REGION_KERNEL_CODE:
        return "kernel_code";
    case BOOT_MAPPING_REGION_KERNEL_DATA:
        return "kernel_data";
    case BOOT_MAPPING_REGION_KERNEL_STACK:
        return "kernel_stack";
    case BOOT_MAPPING_REGION_KERNEL_IST_STACK:
        return "kernel_ist_stack";
    case BOOT_MAPPING_REGION_FRAMEBUFFER:
        return "framebuffer";
    case BOOT_MAPPING_REGION_HPET_MMIO:
        return "hpet_mmio";
    case BOOT_MAPPING_REGION_LAPIC_MMIO:
        return "lapic_mmio";
    default:
        return "unknown";
    }
}

static const char *
VmmKvaKindName(KE_KVA_ADDRESS_KIND kind)
{
    switch (kind)
    {
    case KE_KVA_ADDRESS_OUTSIDE:
        return "outside_kva";
    case KE_KVA_ADDRESS_FREE_HOLE:
        return "free_hole";
    case KE_KVA_ADDRESS_GUARD_PAGE:
        return "guard_page";
    case KE_KVA_ADDRESS_ACTIVE_STACK:
        return "active_stack";
    case KE_KVA_ADDRESS_ACTIVE_FIXMAP:
        return "active_fixmap";
    case KE_KVA_ADDRESS_ACTIVE_HEAP:
        return "active_heap";
    default:
        return "unknown";
    }
}

static void
PrintVmmDiagnosis(const KE_VA_DIAGNOSIS *diagnosis)
{
    if (!diagnosis)
        return;

    const KE_KVA_ADDRESS_INFO *kvaInfo = &diagnosis->KvaInfo;

    if (diagnosis->ImportedRegionMatched && diagnosis->ImportedRegion)
    {
        const KE_IMPORTED_REGION *region = diagnosis->ImportedRegion;
        kprintf("VMM imported: type=%s(%u) va=[%p,%p) pa=[%p,%p)\n", VmmRegionTypeName(region->Type), region->Type,
                (void *)(uint64_t)region->VirtualStart, (void *)(uint64_t)region->VirtualEndExclusive,
                (void *)(uint64_t)region->PhysicalStart, (void *)(uint64_t)region->PhysicalEndExclusive);
    }
    else
    {
        kprintf("VMM imported: none\n");
    }

    if (diagnosis->PtStatus != EC_SUCCESS)
    {
        kprintf("VMM pt: unavailable (%s, %p)\n", KrGetStatusMessage(diagnosis->PtStatus), diagnosis->PtStatus);
    }
    else if (!diagnosis->PtMapping.Present)
    {
        kprintf("VMM pt: not-present\n");
    }
    else
    {
        kprintf("VMM pt: present=1 level=%u page=%lu phys=%p attrs=%p\n", diagnosis->PtMapping.Level,
                (unsigned long)diagnosis->PtMapping.PageSize, (void *)(uint64_t)diagnosis->PtMapping.PhysicalBase,
                (void *)(uint64_t)diagnosis->PtMapping.Attributes);
    }

    if (diagnosis->KvaStatus != EC_SUCCESS && !kvaInfo->InKvaArena && kvaInfo->Kind == KE_KVA_ADDRESS_OUTSIDE &&
        !kvaInfo->HasRange)
    {
        kprintf("VMM kva: unavailable (%s, %p)\n", KrGetStatusMessage(diagnosis->KvaStatus), diagnosis->KvaStatus);
        return;
    }

    kprintf("VMM kva: %s", VmmKvaKindName(kvaInfo->Kind));
    if (kvaInfo->InKvaArena)
    {
        kprintf(" arena=%u page=%lu", kvaInfo->Arena, (unsigned long)kvaInfo->ArenaPageIndex);
    }
    if (diagnosis->KvaStatus != EC_SUCCESS)
    {
        kprintf(" accounting=%s (%p)", KrGetStatusMessage(diagnosis->KvaStatus), diagnosis->KvaStatus);
    }
    kprintf("\n");

    if (kvaInfo->HasRange)
    {
        kprintf("VMM kva range: arena=%u va=[%p,%p) usable=[%p,%p)\n", kvaInfo->Range.Arena,
                (void *)(uint64_t)kvaInfo->Range.BaseAddress,
                (void *)(uint64_t)(kvaInfo->Range.BaseAddress + kvaInfo->Range.TotalPages * PAGE_4KB),
                (void *)(uint64_t)kvaInfo->Range.UsableBase,
                (void *)(uint64_t)(kvaInfo->Range.UsableBase + kvaInfo->Range.UsablePages * PAGE_4KB));
    }
}

static void
ShowCpuExceptionInfo(int64_t vc, const HO_CPU_EXCEPTION_CONTEXT *context)
{
    INTERRUPT_FRAME *frame = NULL;
    if (context)
        frame = (INTERRUPT_FRAME *)context->Frame;
    if (!frame)
        return;

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
    kprintf("RIP: %p RSP: %p RFL: %p\n", frame->RIP, frame->RSP, frame->RFLAGS);
    kprintf("CS:  %p SS:  %p\n", frame->CS, frame->SS);

    if (vc == 14 && context && context->HasFaultAddress)
    {
        kprintf("CR2: %p\n", (void *)(uint64_t)context->FaultAddress);
        PrintPageFaultErrorBits(context->PageFaultErrorCode);

        KE_VA_DIAGNOSIS diagnosis;
        HO_STATUS status = KeDiagnoseVirtualAddress(NULL, context->FaultAddress, &diagnosis);
        if (status == EC_SUCCESS)
        {
            PrintVmmDiagnosis(&diagnosis);
        }
        else
        {
            kprintf("VMM diagnosis failed: %s (%p)\n", KrGetStatusMessage(status), status);
        }
    }
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
        ShowCpuExceptionInfo(-ec, (const HO_CPU_EXCEPTION_CONTEXT *)dump);
    }
    else
    {
        ShowKernelPanicInfo(ec, dump);
    }

    Halt();
}
