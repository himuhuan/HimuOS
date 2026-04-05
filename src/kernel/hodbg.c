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
GetBootMappingRegionName(uint16_t type)
{
    switch (type)
    {
    case BOOT_MAPPING_REGION_IDENTITY:
        return "identity";
    case BOOT_MAPPING_REGION_HHDM:
        return "hhdm";
    case BOOT_MAPPING_REGION_BOOT_STAGING:
        return "boot-staging";
    case BOOT_MAPPING_REGION_BOOT_HANDOFF:
        return "boot-handoff";
    case BOOT_MAPPING_REGION_BOOT_PAGE_TABLES:
        return "boot-page-tables";
    case BOOT_MAPPING_REGION_ACPI_RSDP:
        return "acpi-rsdp";
    case BOOT_MAPPING_REGION_ACPI_ROOT:
        return "acpi-root";
    case BOOT_MAPPING_REGION_ACPI_TABLE:
        return "acpi-table";
    case BOOT_MAPPING_REGION_KERNEL_CODE:
        return "kernel-code";
    case BOOT_MAPPING_REGION_KERNEL_DATA:
        return "kernel-data";
    case BOOT_MAPPING_REGION_KERNEL_STACK:
        return "kernel-stack";
    case BOOT_MAPPING_REGION_KERNEL_IST_STACK:
        return "kernel-ist-stack";
    case BOOT_MAPPING_REGION_FRAMEBUFFER:
        return "framebuffer";
    case BOOT_MAPPING_REGION_HPET_MMIO:
        return "hpet-mmio";
    case BOOT_MAPPING_REGION_LAPIC_MMIO:
        return "lapic-mmio";
    default:
        return "unknown";
    }
}

static const char *
GetKvaArenaName(KE_KVA_ARENA_TYPE arena)
{
    switch (arena)
    {
    case KE_KVA_ARENA_STACK:
        return "stack";
    case KE_KVA_ARENA_FIXMAP:
        return "fixmap";
    case KE_KVA_ARENA_HEAP:
        return "heap";
    default:
        return "unknown";
    }
}

static const char *
GetKvaAddressKindName(KE_KVA_ADDRESS_KIND kind)
{
    switch (kind)
    {
    case KE_KVA_ADDRESS_OUTSIDE:
        return "outside";
    case KE_KVA_ADDRESS_FREE_HOLE:
        return "free-hole";
    case KE_KVA_ADDRESS_GUARD_PAGE:
        return "guard-page";
    case KE_KVA_ADDRESS_ACTIVE_STACK:
        return "active-stack";
    case KE_KVA_ADDRESS_ACTIVE_FIXMAP:
        return "active-fixmap";
    case KE_KVA_ADDRESS_ACTIVE_HEAP:
        return "active-heap";
    case KE_KVA_ADDRESS_UNKNOWN:
        return "unknown";
    default:
        return "invalid";
    }
}

static const char *
GetAllocatorAllocationKindName(KE_ALLOCATOR_ALLOCATION_KIND kind)
{
    switch (kind)
    {
    case KE_ALLOCATOR_ALLOCATION_SMALL:
        return "small";
    case KE_ALLOCATOR_ALLOCATION_LARGE:
        return "large";
    case KE_ALLOCATOR_ALLOCATION_UNKNOWN:
    default:
        return "unknown";
    }
}

static void
PrintImportedRegionDiagnosis(const KE_VA_DIAGNOSIS *diagnosis)
{
    if (diagnosis->ImportedStatus != EC_SUCCESS)
    {
        kprintf("VMM imported: unavailable (%s, %p)\n", KrGetStatusMessage(diagnosis->ImportedStatus),
                diagnosis->ImportedStatus);
        return;
    }

    if (!diagnosis->ImportedRegionMatched || !diagnosis->ImportedRegion)
    {
        kprintf("VMM imported: no matching imported region\n");
        return;
    }

    const KE_IMPORTED_REGION *region = diagnosis->ImportedRegion;
    kprintf("VMM imported: type=%s va=[%p - %p) pa=[%p - %p)\n", GetBootMappingRegionName(region->Type),
            (void *)(uint64_t)region->VirtualStart, (void *)(uint64_t)region->VirtualEndExclusive,
            (void *)(uint64_t)region->PhysicalStart, (void *)(uint64_t)region->PhysicalEndExclusive);
}

static void
PrintPtDiagnosis(const KE_VA_DIAGNOSIS *diagnosis)
{
    if (diagnosis->PtStatus != EC_SUCCESS)
    {
        kprintf("VMM pt: unavailable (%s, %p)\n", KrGetStatusMessage(diagnosis->PtStatus), diagnosis->PtStatus);
        return;
    }

    if (!diagnosis->PtMapping.Present)
    {
        kprintf("VMM pt: unmapped\n");
        return;
    }

    kprintf("VMM pt: present phys=%p pageSize=%lu level=%u attrs=0x%lx%s\n",
            (void *)(uint64_t)diagnosis->PtMapping.PhysicalBase, diagnosis->PtMapping.PageSize,
            (unsigned int)diagnosis->PtMapping.Level, diagnosis->PtMapping.Attributes,
            diagnosis->PtMapping.LargeLeaf ? " large-leaf" : "");
}

static void
PrintKvaDiagnosis(const KE_VA_DIAGNOSIS *diagnosis)
{
    if (diagnosis->KvaStatus != EC_SUCCESS && !diagnosis->KvaInfo.InKvaArena)
    {
        kprintf("VMM kva: unavailable (%s, %p)\n", KrGetStatusMessage(diagnosis->KvaStatus), diagnosis->KvaStatus);
        return;
    }

    if (!diagnosis->KvaInfo.InKvaArena)
    {
        kprintf("VMM kva: outside kva arenas\n");
        return;
    }

    if (diagnosis->KvaStatus != EC_SUCCESS)
    {
        kprintf("VMM kva: partial kind=%s arena=%s page=%lu status=%s (%p)\n",
                GetKvaAddressKindName(diagnosis->KvaInfo.Kind), GetKvaArenaName(diagnosis->KvaInfo.Arena),
                diagnosis->KvaInfo.ArenaPageIndex, KrGetStatusMessage(diagnosis->KvaStatus), diagnosis->KvaStatus);
        return;
    }

    if (diagnosis->KvaInfo.HasRange)
    {
        HO_VIRTUAL_ADDRESS rangeEnd =
            diagnosis->KvaInfo.Range.BaseAddress + diagnosis->KvaInfo.Range.TotalPages * PAGE_4KB;
        HO_VIRTUAL_ADDRESS usableEnd =
            diagnosis->KvaInfo.Range.UsableBase + diagnosis->KvaInfo.Range.UsablePages * PAGE_4KB;

        kprintf("VMM kva: kind=%s arena=%s window=[%p - %p) usable=[%p - %p)\n",
                GetKvaAddressKindName(diagnosis->KvaInfo.Kind), GetKvaArenaName(diagnosis->KvaInfo.Arena),
                (void *)(uint64_t)diagnosis->KvaInfo.Range.BaseAddress, (void *)(uint64_t)rangeEnd,
                (void *)(uint64_t)diagnosis->KvaInfo.Range.UsableBase, (void *)(uint64_t)usableEnd);
        return;
    }

    kprintf("VMM kva: kind=%s arena=%s page=%lu arenaWindow=[%p - %p)\n",
            GetKvaAddressKindName(diagnosis->KvaInfo.Kind), GetKvaArenaName(diagnosis->KvaInfo.Arena),
            diagnosis->KvaInfo.ArenaPageIndex, (void *)(uint64_t)diagnosis->KvaInfo.ArenaBase,
            (void *)(uint64_t)diagnosis->KvaInfo.ArenaEndExclusive);
}

static void
PrintAllocatorDiagnosis(const KE_VA_DIAGNOSIS *diagnosis)
{
    if (diagnosis->KvaStatus != EC_SUCCESS || diagnosis->KvaInfo.Kind != KE_KVA_ADDRESS_ACTIVE_HEAP)
        return;

    if (diagnosis->AllocatorStatus != EC_SUCCESS)
    {
        kprintf("VMM allocator: unavailable (%s, %p)\n", KrGetStatusMessage(diagnosis->AllocatorStatus),
                diagnosis->AllocatorStatus);
        return;
    }

    if (!diagnosis->AllocatorInfo.LiveAllocation)
    {
        kprintf("VMM allocator: unknown (active heap but no live allocation metadata)\n");
        return;
    }

    const char *kindName = GetAllocatorAllocationKindName(diagnosis->AllocatorInfo.Kind);
    kprintf("VMM allocator: kind=%s req=%lu alloc=[%p - %p) backing=[%p pages=%lu]\n", kindName,
            diagnosis->AllocatorInfo.RequestedSize, (void *)(uint64_t)diagnosis->AllocatorInfo.AllocationBase,
            (void *)(uint64_t)diagnosis->AllocatorInfo.AllocationEndExclusive,
            (void *)(uint64_t)diagnosis->AllocatorInfo.BackingUsableBase, diagnosis->AllocatorInfo.BackingUsablePages);

    if (diagnosis->AllocatorInfo.Kind == KE_ALLOCATOR_ALLOCATION_SMALL)
    {
        kprintf("VMM allocator: small classIndex=%u classSize=%u\n", diagnosis->AllocatorInfo.SmallClassIndex,
                diagnosis->AllocatorInfo.SmallClassSize);
    }
}

static void
PrintPageFaultDiagnosis(const HO_CPU_EXCEPTION_CONTEXT *context)
{
    if (!context || !context->HasFaultAddress)
        return;

    if (!context->IsSafePageFaultContext)
    {
        kprintf("VMM diag: skipped (not running on the page-fault diagnostic IST)\n");
        return;
    }

    KE_VA_DIAGNOSIS diagnosis;
    HO_STATUS status = KeDiagnoseVirtualAddress(NULL, context->FaultAddress, &diagnosis);
    if (status != EC_SUCCESS)
    {
        kprintf("VMM diag: unavailable (%s, %p)\n", KrGetStatusMessage(status), status);
        return;
    }

    PrintImportedRegionDiagnosis(&diagnosis);
    PrintPtDiagnosis(&diagnosis);
    PrintKvaDiagnosis(&diagnosis);
    PrintAllocatorDiagnosis(&diagnosis);
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
        PrintPageFaultDiagnosis(context);
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
    ConsoleFlush();
    ConsoleClearScreen(COLOR_BLUE);

    if (ec <= 0)
    {
        ShowCpuExceptionInfo(-ec, (const HO_CPU_EXCEPTION_CONTEXT *)dump);
    }
    else
    {
        ShowKernelPanicInfo(ec, dump);
    }

    ConsoleFlush();
    Halt();
}
