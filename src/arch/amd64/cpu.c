#include <arch/arch.h>
#include <arch/amd64/cpu.h>
#include <arch/amd64/asm.h>
#include <libc/string.h>

HO_KERNEL_API void
x64_Halt(void)
{
    asm volatile("cli");
    while (TRUE)
        asm volatile("hlt");
}

HO_KERNEL_API void
x64_GetBasicCpuInfo(ARCH_BASIC_CPU_INFO *info)
{
    uint32_t eax, ebx, ecx, edx;

    info->TimerFeatures = ARCH_TIMER_FEAT_NONE;
    cpuid(0x1, &eax, &ebx, &ecx, &edx);
    info->IsRunningInHypervisor = ecx & (1 << 31);
    if (ecx & (1 << 24))
        info->TimerFeatures |= ARCH_TIMER_FEAT_TSC_DEADLINE;
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
