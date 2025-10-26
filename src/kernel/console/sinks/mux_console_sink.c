#include "mux_console_sink.h"

static HO_STATUS MuxConSinkGetInfo(void *self, CONSOLE_SINK_INFO *info)
{
    MUX_CONSOLE_SINK *sink = (MUX_CONSOLE_SINK *)self;
    if (!sink || !info)
        return EC_ILLEGAL_ARGUMENT;

    // the first sink is considered the primary one
    return sink->Sinks[0]->GetInfo(sink->Sinks[0], info);
}

static int MuxConSinkPutChar(void *self, uint16_t gx, uint16_t gy, char c, COLOR32 fg, COLOR32 bg)
{
    MUX_CONSOLE_SINK *sink = (MUX_CONSOLE_SINK *)self;
    if (!sink)
        return -1;

    int result = 0;
    for (size_t i = 0; i < sink->SinkCount; ++i)
    {
        int res = sink->Sinks[i]->PutChar(sink->Sinks[i], gx, gy, c, fg, bg);
        if (res < 0)
            result = res; // propagate error if any
    }
    return result;
}

static HO_STATUS MuxConSinkScroll(void *self, uint16_t count, COLOR32 fillColor)
{
    MUX_CONSOLE_SINK *sink = (MUX_CONSOLE_SINK *)self;
    if (!sink)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = EC_SUCCESS;
    for (size_t i = 0; i < sink->SinkCount; ++i)
    {
        HO_STATUS st = sink->Sinks[i]->Scroll(sink->Sinks[i], count, fillColor);
        if (st != EC_SUCCESS)
            status = st; // propagate error if any
    }
    return status;
}

HO_KERNEL_API HO_STATUS
MuxConSinkInit(MUX_CONSOLE_SINK *sink)
{
    memset(sink, 0, sizeof(MUX_CONSOLE_SINK));
    sink->Base.GetInfo = MuxConSinkGetInfo;
    sink->Base.PutChar = MuxConSinkPutChar;
    sink->Base.Scroll = MuxConSinkScroll;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
MuxConSinkAddSink(MUX_CONSOLE_SINK *muxSink, CONSOLE_SINK *sink)
{
    if (muxSink->SinkCount >= MAX_MUX_SINKS)
        return EC_OUT_OF_RESOURCE;
    muxSink->Sinks[muxSink->SinkCount++] = sink;
    return EC_SUCCESS;
}