#include "serial_console_sink.h"

static HO_STATUS
SerialConSinkGetInfo(MAYBE_UNUSED void *self, CONSOLE_SINK_INFO *info)
{
    info->CharWidth = info->CharHeight = info->Scale = 0; // meaningless for serial
    info->CharPerTab = 4;
    info->GridWidth = 80;  // typical width
    info->GridHeight = 24; // typical height
    return EC_SUCCESS;
}

static HO_STATUS
SerialConSinkPutChar(
    void *self, MAYBE_UNUSED uint16_t x, uint16_t y, char c, MAYBE_UNUSED COLOR32 fg, MAYBE_UNUSED COLOR32 bg)
{
    SERIAL_CONSOLE_SINK *sink = (SERIAL_CONSOLE_SINK *)self;

    while (sink->CurrentRow < y)
    {
        SerialWriteByte(sink->Port, '\n');
        sink->CurrentRow++;
    }
    SerialWriteByte(sink->Port, c);
    return EC_SUCCESS;
}

static HO_STATUS
SerialConSinkScroll(MAYBE_UNUSED void *self, MAYBE_UNUSED uint16_t count, MAYBE_UNUSED COLOR32 fill)
{
    SERIAL_CONSOLE_SINK *s = (SERIAL_CONSOLE_SINK *)self;
    while (count-- > 0)
    {
        SerialWriteByte(s->Port, '\n');
        s->CurrentRow++;
    }
    return EC_SUCCESS;
}

void
SerialConSinkInit(SERIAL_CONSOLE_SINK *sink, uint16_t port)
{
    sink->Base.GetInfo = SerialConSinkGetInfo;
    sink->Base.PutChar = SerialConSinkPutChar;
    sink->Base.Scroll = SerialConSinkScroll;
    sink->Port = port;
    sink->CurrentRow = 0;
}