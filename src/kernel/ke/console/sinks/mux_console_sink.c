/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/mux_console_sink.c
 * Description:
 * Ke Layer - Multiplexed console sink implementation
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "mux_console_sink.h"
#include <kernel/ke/mm.h>

static HO_STATUS
MuxConSinkGetInfo(void *self, CONSOLE_SINK_INFO *info)
{
    MUX_CONSOLE_SINK *sink = (MUX_CONSOLE_SINK *)self;
    if (!sink || !info)
        return EC_ILLEGAL_ARGUMENT;
    if (sink->SinkCount == 0 || sink->Sinks == NULL || sink->Sinks[0] == NULL)
        return EC_INVALID_STATE;

    return sink->Sinks[0]->GetInfo(sink->Sinks[0], info);
}

static int
MuxConSinkPutChar(void *self, uint16_t gx, uint16_t gy, char c, COLOR32 fg, COLOR32 bg)
{
    MUX_CONSOLE_SINK *sink = (MUX_CONSOLE_SINK *)self;
    if (!sink)
        return -1;

    int result = 0;
    for (size_t i = 0; i < sink->SinkCount; ++i)
    {
        int res = sink->Sinks[i]->PutChar(sink->Sinks[i], gx, gy, c, fg, bg);
        if (res < 0)
            result = res;
    }
    return result;
}

static HO_STATUS
MuxConSinkScroll(void *self, uint16_t count, COLOR32 fillColor)
{
    MUX_CONSOLE_SINK *sink = (MUX_CONSOLE_SINK *)self;
    if (!sink)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = EC_SUCCESS;
    for (size_t i = 0; i < sink->SinkCount; ++i)
    {
        HO_STATUS st = sink->Sinks[i]->Scroll(sink->Sinks[i], count, fillColor);
        if (st != EC_SUCCESS)
            status = st;
    }
    return status;
}

static HO_STATUS
MuxConSinkClear(void *self, COLOR32 fillColor)
{
    MUX_CONSOLE_SINK *sink = (MUX_CONSOLE_SINK *)self;
    if (!sink)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = EC_SUCCESS;
    for (size_t i = 0; i < sink->SinkCount; ++i)
    {
        if (sink->Sinks[i]->Clear == NULL)
            continue;
        HO_STATUS st = sink->Sinks[i]->Clear(sink->Sinks[i], fillColor);
        if (st != EC_SUCCESS)
            status = st;
    }
    return status;
}

HO_KERNEL_API HO_STATUS
KeMuxConSinkInit(MUX_CONSOLE_SINK *sink)
{
    if (!sink)
        return EC_ILLEGAL_ARGUMENT;

    memset(sink, 0, sizeof(MUX_CONSOLE_SINK));
    sink->Sinks = sink->BootstrapSinks;
    sink->SinkCapacity = MAX_MUX_SINKS;
    sink->UsesAllocatorStorage = FALSE;
    sink->Base.GetInfo = MuxConSinkGetInfo;
    sink->Base.PutChar = MuxConSinkPutChar;
    sink->Base.Scroll = MuxConSinkScroll;
    sink->Base.Clear = MuxConSinkClear;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
KeMuxConSinkAddSink(MUX_CONSOLE_SINK *muxSink, KE_CONSOLE_SINK *sink)
{
    if (!muxSink || !sink || muxSink->Sinks == NULL)
        return EC_ILLEGAL_ARGUMENT;
    if (muxSink->SinkCount >= muxSink->SinkCapacity)
        return EC_OUT_OF_RESOURCE;
    muxSink->Sinks[muxSink->SinkCount++] = sink;
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
KeMuxConSinkPromoteToAllocator(MUX_CONSOLE_SINK *muxSink)
{
    if (!muxSink || muxSink->Sinks == NULL)
        return EC_ILLEGAL_ARGUMENT;
    if (muxSink->UsesAllocatorStorage)
        return EC_SUCCESS;
    if (muxSink->SinkCapacity == 0)
        return EC_INVALID_STATE;

    KE_CONSOLE_SINK **allocatorStorage = (KE_CONSOLE_SINK **)kzalloc(muxSink->SinkCapacity * sizeof(KE_CONSOLE_SINK *));
    if (!allocatorStorage)
        return EC_OUT_OF_RESOURCE;

    if (muxSink->SinkCount > 0)
        memcpy(allocatorStorage, muxSink->Sinks, muxSink->SinkCount * sizeof(KE_CONSOLE_SINK *));

    muxSink->Sinks = allocatorStorage;
    muxSink->UsesAllocatorStorage = TRUE;
    return EC_SUCCESS;
}
