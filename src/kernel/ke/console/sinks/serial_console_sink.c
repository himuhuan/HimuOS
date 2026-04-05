/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/serial_console_sink.c
 * Description:
 * Ke Layer - Serial console sink implementation
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "serial_console_sink.h"

static inline void
SerialConSinkEmitCrlf(SERIAL_CONSOLE_SINK *sink)
{
    SerialWriteByte(sink->Port, '\r');
    SerialWriteByte(sink->Port, '\n');
    sink->CurrentColumn = 0;
}

static inline void
SerialConSinkAdvanceCursor(SERIAL_CONSOLE_SINK *sink, uint16_t x, uint16_t y)
{
    if (sink->CurrentRow > y)
    {
        if (sink->CurrentColumn != 0)
            SerialConSinkEmitCrlf(sink);

        sink->CurrentRow = 0;
        sink->CurrentColumn = 0;
    }

    while (sink->CurrentRow < y)
    {
        SerialConSinkEmitCrlf(sink);
        sink->CurrentRow++;
    }

    if (x < sink->CurrentColumn)
    {
        SerialWriteByte(sink->Port, '\r');
        sink->CurrentColumn = 0;
    }

    while (sink->CurrentColumn < x)
    {
        SerialWriteByte(sink->Port, ' ');
        sink->CurrentColumn++;
    }
}

static HO_STATUS
SerialConSinkGetInfo(MAYBE_UNUSED void *self, CONSOLE_SINK_INFO *info)
{
    info->CharWidth = info->CharHeight = info->Scale = 0;
    info->CharPerTab = 4;
    info->GridWidth = 80;
    info->GridHeight = 24;
    return EC_SUCCESS;
}

static HO_STATUS
SerialConSinkPutChar(
    void *self, MAYBE_UNUSED uint16_t x, uint16_t y, char c, MAYBE_UNUSED COLOR32 fg, MAYBE_UNUSED COLOR32 bg)
{
    SERIAL_CONSOLE_SINK *sink = (SERIAL_CONSOLE_SINK *)self;

    SerialConSinkAdvanceCursor(sink, x, y);
    SerialWriteByte(sink->Port, c);
    sink->CurrentColumn++;
    return EC_SUCCESS;
}

static HO_STATUS
SerialConSinkScroll(MAYBE_UNUSED void *self, MAYBE_UNUSED uint16_t count, MAYBE_UNUSED COLOR32 fill)
{
    SERIAL_CONSOLE_SINK *s = (SERIAL_CONSOLE_SINK *)self;
    while (count-- > 0)
    {
        SerialConSinkEmitCrlf(s);
        s->CurrentRow++;
    }
    return EC_SUCCESS;
}

static HO_STATUS
SerialConSinkClear(void *self, MAYBE_UNUSED COLOR32 fillColor)
{
    SERIAL_CONSOLE_SINK *sink = (SERIAL_CONSOLE_SINK *)self;
    if (!sink)
        return EC_ILLEGAL_ARGUMENT;

    if (sink->CurrentColumn != 0)
        SerialConSinkEmitCrlf(sink);

    sink->CurrentRow = 0;
    sink->CurrentColumn = 0;
    return EC_SUCCESS;
}

void
KeSerialConSinkInit(SERIAL_CONSOLE_SINK *sink, uint16_t port)
{
    sink->Base.GetInfo = SerialConSinkGetInfo;
    sink->Base.PutChar = SerialConSinkPutChar;
    sink->Base.Scroll = SerialConSinkScroll;
    sink->Base.Clear = SerialConSinkClear;
    sink->Port = port;
    sink->CurrentRow = 0;
    sink->CurrentColumn = 0;
}

void
KeSerialConSinkSyncCursor(SERIAL_CONSOLE_SINK *sink, uint16_t x, uint16_t y)
{
    if (!sink)
        return;

    SerialConSinkAdvanceCursor(sink, x, y);
}
