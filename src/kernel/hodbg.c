#include <kernel/hodbg.h>
#include <kernel/console.h>

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

HO_PUBLIC_API HO_NORETURN void
KernelHalt(int64_t ec, void *dump)
{
    if (ec < 0)
    {
        kprintf("A CPU exception has occurred! Vector: %lld\n", -ec);
        (void)dump; // Currently not used, can be extended to show more info
    }

    for (;;)
        __asm__ volatile("hlt");
}