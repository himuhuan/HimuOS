#include <kernel/hodbg.h>
#include <lib/tui/text_sink.h>

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
        "Operation not supported"
    };
    // clang-format on
    uint64_t index = (uint64_t)status;
    if (index >= sizeof(kStatusMessages) / sizeof(kStatusMessages[0]))
        return "Unknown error code";
    return kStatusMessages[index];
}

void KrPrintStautsMessage(const char *msg, HO_STATUS status)
{
    TEXT_RENDER_SINK *sink = TTY0_SINK;
    COLOR32 fg, bg;
    TrSinkGetColor(sink, &fg, &bg);

    kprintf("%s... ", msg);
    TrSinkSetAlign((TEXT_RENDER_SINK *)sink, 10, TR_PUTS_ALIGN_RIGHT);
    if (status == EC_SUCCESS)
    {
        TrSinkSetColor(sink, COLOR_GREEN, bg);
        ConPutStr("[OK]\n");
    }
    else
    {
        TrSinkSetColor(sink, COLOR_RED, bg);
        kprintf("[FAIL: %d]\n", status);
    }

    TrSinkSetColor(sink, fg, bg);
}

void KrPrintHexMessage(const char *msg, uint64_t value)
{
    TEXT_RENDER_SINK *sink = TTY0_SINK;
    kprintf("%s: ", msg);
    TrSinkSetAlign((TEXT_RENDER_SINK *)sink, 20, TR_PUTS_ALIGN_CENTER);
    kprintf("%p\n", value);
}