#include <arch/amd64/cpu.h>

HO_NORETURN void HO_KERNEL_API Halt(void)
{
    asm volatile("cli");
    while (TRUE)
        asm volatile("hlt");
}