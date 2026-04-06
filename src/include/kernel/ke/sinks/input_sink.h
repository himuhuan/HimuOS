/**
 * HimuOperatingSystem
 *
 * File: ke/sinks/input_sink.h
 * Description: Ke Layer - runtime keyboard/input sink interface.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include <_hobase.h>

typedef struct KE_INPUT_SINK
{
    HO_STATUS (*Init)(void *self);
    BOOL (*HasPendingData)(void *self);
    HO_STATUS (*ReadScanCode)(void *self, uint8_t *outScanCode);
    void (*AcknowledgeInterrupt)(void *self);
    const char *(*GetName)(void *self);
} KE_INPUT_SINK;
