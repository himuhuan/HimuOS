#include <arch/arch.h>
#include <arch/amd64/cpu.h>

HO_NORETURN void HO_KERNEL_API
Halt(void)
{
    x64_Halt();
}

HO_KERNEL_API ARCH_INTERRUPT_STATE
ArchGetInterruptState(void)
{
    ARCH_INTERRUPT_STATE state;
    state.MaskableInterruptEnabled = x64_GetInterruptEnabledState();
    return state;
}

HO_KERNEL_API ARCH_INTERRUPT_STATE
ArchDisableInterrupts(void)
{
    ARCH_INTERRUPT_STATE state;
    state.MaskableInterruptEnabled = x64_DisableInterruptsSave();
    return state;
}

HO_KERNEL_API void
ArchRestoreInterruptState(ARCH_INTERRUPT_STATE state)
{
    x64_RestoreInterruptState(state.MaskableInterruptEnabled);
}

void HO_PUBLIC_API
GetBasicCpuInfo(ARCH_BASIC_CPU_INFO *info)
{
    x64_GetBasicCpuInfo(info);
}
