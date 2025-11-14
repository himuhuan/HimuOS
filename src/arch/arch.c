#include <arch/arch.h>
#include <arch/amd64/cpu.h>

HO_NORETURN void HO_KERNEL_API Halt(void)
{
    x64_Halt();
}

void HO_PUBLIC_API GetBasicCpuInfo(ARCH_BASIC_CPU_INFO *info)
{
    x64_GetBasicCpuInfo(info);
}