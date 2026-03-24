#include <arch/arch.h>
#include <arch/amd64/cpu.h>
#include <arch/amd64/asm.h>
#include <libc/string.h>

#define X64_RFLAGS_IF (1ULL << 9)

HO_KERNEL_API void
x64_Halt(void)
{
    x64_Cli();
    while (TRUE)
        asm volatile("hlt");
}

HO_KERNEL_API BOOL
x64_GetInterruptEnabledState(void)
{
    return (x64_ReadRflags() & X64_RFLAGS_IF) != 0;
}

HO_KERNEL_API BOOL
x64_DisableInterruptsSave(void)
{
    BOOL interruptEnabled = x64_GetInterruptEnabledState();
    x64_Cli();
    return interruptEnabled;
}

HO_KERNEL_API void
x64_RestoreInterruptState(BOOL interruptEnabled)
{
    if (interruptEnabled)
        x64_Sti();
    else
        x64_Cli();
}

HO_KERNEL_API void
x64_GetBasicCpuInfo(ARCH_BASIC_CPU_INFO *info)
{
    uint32_t eax, ebx, ecx, edx;

    info->TimerFeatures = ARCH_TIMER_FEAT_NONE;
    cpuid(0x1, &eax, &ebx, &ecx, &edx);
    info->IsRunningInHypervisor = ecx & (1 << 31);
    if (ecx & (1 << 24))
        info->TimerFeatures |= ARCH_TIMER_FEAT_ONE_SHOT;
    if (edx & (1 << 4))
        info->TimerFeatures |= ARCH_TIMER_FEAT_COUNTER;

    cpuid(0x0, &eax, &ebx, &ecx, &edx);
    info->SpecificInfo.X64.MaxLeafSupported = eax;

    memset(info->ModelName, 0, sizeof(info->ModelName));
    cpuid(0x80000000, &info->SpecificInfo.X64.MaxExtLeafSupported, &ebx, &ecx, &edx);

    if (HO_LIKELY(info->SpecificInfo.X64.MaxExtLeafSupported >= 0x80000007))
    {
        cpuid(0x80000007, &eax, &ebx, &ecx, &edx);
        if (edx & (1 << 8)) // Invariant TSC
            info->TimerFeatures |= ARCH_TIMER_FEAT_INVARIANT;
    }

    if (HO_LIKELY(info->SpecificInfo.X64.MaxExtLeafSupported >= 0x80000004))
    {
        uint32_t *namePtr = (uint32_t *)info->ModelName;
        cpuid(0x80000002, &namePtr[0], &namePtr[1], &namePtr[2], &namePtr[3]);
        cpuid(0x80000003, &namePtr[4], &namePtr[5], &namePtr[6], &namePtr[7]);
        cpuid(0x80000004, &namePtr[8], &namePtr[9], &namePtr[10], &namePtr[11]);
    }
    else
    {
        // Extended CPUID leaves not supported; cannot detect CPU model
        strcpy(info->ModelName, "Unknown CPU");
    }
}
