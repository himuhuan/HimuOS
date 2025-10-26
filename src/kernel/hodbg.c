#include <kernel/hodbg.h>

const char *KrGetStatusMessage(HO_STATUS status)
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
