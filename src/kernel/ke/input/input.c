/**
 * HimuOperatingSystem
 *
 * File: ke/input/input.c
 * Description: Bounded runtime keyboard input lane for foreground-owned lines.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include "sinks/ps2_keyboard_sink.h"

#include <arch/amd64/idt.h>
#include <kernel/hodbg.h>
#include <kernel/ke/console.h>
#include <kernel/ke/critical_section.h>
#include <kernel/ke/event.h>
#include <kernel/ke/input.h>
#include <kernel/ke/scheduler.h>
#include <libc/string.h>

#define KE_INPUT_IRQ_VECTOR 0x21U
#define KE_INPUT_OWNER_RECHECK_TIMEOUT_NS 10000000ULL

typedef struct KE_INPUT_DEVICE
{
    KE_INPUT_SINK *ActiveSink;
    void *ActiveSinkContext;
    KEVENT LineReadyEvent;
    char CurrentLine[KE_INPUT_LINE_CAPACITY];
    char CompletedLine[KE_INPUT_LINE_CAPACITY];
    uint32_t CurrentLineLength;
    uint32_t CompletedLineLength;
    uint32_t ForegroundOwnerThreadId;
    uint64_t CompletedReadCount;
    BOOL LineReady;
    BOOL ShiftDown;
    BOOL Initialized;
    uint8_t Vector;
} KE_INPUT_DEVICE;

static KE_INPUT_DEVICE gInputDevice;
static KE_PS2_KEYBOARD_SINK gPs2KeyboardSink;

static void KiKeyboardInterruptHandler(void *frame, void *context);
static char KiTranslateSet1ScanCode(uint8_t scanCode, BOOL shifted);
static void KiHandleTranslatedInputChar(char character);
static HO_STATUS KiValidateForegroundCurrentThread(void);

static const char kPs2Set1AsciiMap[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5', [0x07] = '6', [0x08] = '7', [0x09] = '8',
    [0x0A] = '9', [0x0B] = '0', [0x0C] = '-', [0x0D] = '=', [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
    [0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
    [0x26] = 'l', [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\', [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
    [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/', [0x39] = ' ',
};

static const char kPs2Set1ShiftAsciiMap[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%', [0x07] = '^', [0x08] = '&', [0x09] = '*',
    [0x0A] = '(', [0x0B] = ')', [0x0C] = '_', [0x0D] = '+', [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
    [0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
    [0x26] = 'L', [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2B] = '|', [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C',
    [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?', [0x39] = ' ',
};

static char
KiTranslateSet1ScanCode(uint8_t scanCode, BOOL shifted)
{
    if (scanCode >= 128U)
        return 0;

    if (scanCode == 0x1CU)
        return '\n';

    if (scanCode == 0x0EU)
        return '\b';

    return shifted ? kPs2Set1ShiftAsciiMap[scanCode] : kPs2Set1AsciiMap[scanCode];
}

static void
KiHandleTranslatedInputChar(char character)
{
    if (gInputDevice.ForegroundOwnerThreadId == 0U)
        return;

    if (character == '\b')
    {
        if (gInputDevice.CurrentLineLength == 0U || gInputDevice.LineReady)
            return;

        gInputDevice.CurrentLineLength--;
        (void)ConsoleWriteChar('\b');
        return;
    }

    if (character == '\n')
    {
        if (gInputDevice.LineReady)
            return;

        if (gInputDevice.CurrentLineLength != 0U)
            memcpy(gInputDevice.CompletedLine, gInputDevice.CurrentLine, gInputDevice.CurrentLineLength);

        gInputDevice.CompletedLineLength = gInputDevice.CurrentLineLength;
        gInputDevice.LineReady = TRUE;
        gInputDevice.CurrentLineLength = 0U;
        memset(gInputDevice.CurrentLine, 0, sizeof(gInputDevice.CurrentLine));

        (void)ConsoleWriteChar('\n');
        KeSetEvent(&gInputDevice.LineReadyEvent);
        klog(KLOG_LEVEL_INFO,
             "[INPUT] line ready bytes=%u owner=%u\n",
             gInputDevice.CompletedLineLength,
             gInputDevice.ForegroundOwnerThreadId);
        return;
    }

    if (character < ' ' || character > '~' || gInputDevice.LineReady)
        return;

    if (gInputDevice.CurrentLineLength >= KE_INPUT_LINE_CAPACITY)
        return;

    gInputDevice.CurrentLine[gInputDevice.CurrentLineLength++] = character;
    (void)ConsoleWriteChar(character);
}

static HO_STATUS
KiValidateForegroundCurrentThread(void)
{
    KTHREAD *currentThread = KeGetCurrentThread();

    if (currentThread == NULL)
        return EC_INVALID_STATE;

    if (gInputDevice.ForegroundOwnerThreadId == 0U || currentThread->ThreadId != gInputDevice.ForegroundOwnerThreadId)
        return EC_INVALID_STATE;

    return EC_SUCCESS;
}

static void
KiKeyboardInterruptHandler(MAYBE_UNUSED void *frame, void *context)
{
    KE_INPUT_SINK *sink = (KE_INPUT_SINK *)context;
    KE_CRITICAL_SECTION guard = {0};

    if (sink == NULL || sink->ReadScanCode == NULL || sink->AcknowledgeInterrupt == NULL)
        return;

    KeEnterCriticalSection(&guard);

    while (sink->HasPendingData != NULL && sink->HasPendingData(gInputDevice.ActiveSinkContext))
    {
        uint8_t scanCode = 0;
        if (sink->ReadScanCode(gInputDevice.ActiveSinkContext, &scanCode) != EC_SUCCESS)
            break;

        if (scanCode == 0x2AU || scanCode == 0x36U)
        {
            gInputDevice.ShiftDown = TRUE;
            continue;
        }

        if (scanCode == 0xAAU || scanCode == 0xB6U)
        {
            gInputDevice.ShiftDown = FALSE;
            continue;
        }

        if ((scanCode & 0x80U) != 0)
            continue;

        char character = KiTranslateSet1ScanCode(scanCode, gInputDevice.ShiftDown);
        if (character != 0)
            KiHandleTranslatedInputChar(character);
    }

    sink->AcknowledgeInterrupt(gInputDevice.ActiveSinkContext);
    KeLeaveCriticalSection(&guard);
}

HO_KERNEL_API HO_STATUS
KeInputInit(void)
{
    HO_STATUS status = KePs2KeyboardSinkInit(&gPs2KeyboardSink);
    if (status != EC_SUCCESS)
        return status;

    memset(&gInputDevice, 0, sizeof(gInputDevice));
    gInputDevice.ActiveSink = &gPs2KeyboardSink.Base;
    gInputDevice.ActiveSinkContext = &gPs2KeyboardSink;
    gInputDevice.Vector = KE_INPUT_IRQ_VECTOR;
    KeInitializeEvent(&gInputDevice.LineReadyEvent, FALSE);

    status = gInputDevice.ActiveSink->Init(gInputDevice.ActiveSinkContext);
    if (status != EC_SUCCESS)
        return status;

    status = IdtRegisterInterruptHandler(gInputDevice.Vector, KiKeyboardInterruptHandler, gInputDevice.ActiveSink);
    if (status != EC_SUCCESS)
        return status;

    gInputDevice.Initialized = TRUE;
    klog(KLOG_LEVEL_INFO,
         "[INPUT] ready source=%s vector=%u line_cap=%u\n",
         gInputDevice.ActiveSink->GetName(gInputDevice.ActiveSinkContext),
         gInputDevice.Vector,
         KE_INPUT_LINE_CAPACITY);
    return EC_SUCCESS;
}

HO_KERNEL_API BOOL
KeInputIsReady(void)
{
    return gInputDevice.Initialized;
}

HO_KERNEL_API const char *
KeInputGetSourceName(void)
{
    if (!gInputDevice.Initialized || gInputDevice.ActiveSink == NULL || gInputDevice.ActiveSink->GetName == NULL)
        return NULL;

    return gInputDevice.ActiveSink->GetName(gInputDevice.ActiveSinkContext);
}

HO_KERNEL_API uint8_t
KeInputGetVector(void)
{
    return gInputDevice.Vector;
}

HO_KERNEL_API HO_STATUS
KeInputSetForegroundOwnerThreadId(uint32_t threadId)
{
    KE_CRITICAL_SECTION guard = {0};

    if (!gInputDevice.Initialized)
        return EC_INVALID_STATE;

    KeEnterCriticalSection(&guard);
    gInputDevice.ForegroundOwnerThreadId = threadId;
    gInputDevice.CurrentLineLength = 0U;
    gInputDevice.CompletedLineLength = 0U;
    gInputDevice.LineReady = FALSE;
    gInputDevice.ShiftDown = FALSE;
    memset(gInputDevice.CurrentLine, 0, sizeof(gInputDevice.CurrentLine));
    memset(gInputDevice.CompletedLine, 0, sizeof(gInputDevice.CompletedLine));
    KeResetEvent(&gInputDevice.LineReadyEvent);
    KeLeaveCriticalSection(&guard);

    klog(KLOG_LEVEL_INFO, "[INPUT] foreground owner=%u\n", threadId);
    return EC_SUCCESS;
}

HO_KERNEL_API uint32_t
KeInputGetForegroundOwnerThreadId(void)
{
    return gInputDevice.ForegroundOwnerThreadId;
}

HO_KERNEL_API uint64_t
KeInputGetCompletedReadCount(void)
{
    return gInputDevice.CompletedReadCount;
}

HO_KERNEL_API HO_STATUS
KeInputWaitForForegroundLine(void)
{
    if (!gInputDevice.Initialized)
        return EC_INVALID_STATE;

    for (;;)
    {
        HO_STATUS status = KiValidateForegroundCurrentThread();
        if (status != EC_SUCCESS)
            return status;

        status = KeWaitForSingleObject(&gInputDevice.LineReadyEvent, KE_INPUT_OWNER_RECHECK_TIMEOUT_NS);
        if (status == EC_SUCCESS)
        {
            status = KiValidateForegroundCurrentThread();
            return status == EC_SUCCESS ? EC_SUCCESS : status;
        }

        if (status != EC_TIMEOUT)
            return status;
    }
}

HO_KERNEL_API HO_STATUS
KeInputCopyCompletedLineForCurrentThread(char *buffer, uint32_t capacity, uint32_t *outLength)
{
    KE_CRITICAL_SECTION guard = {0};

    if (buffer == NULL || outLength == NULL || capacity == 0U)
        return EC_ILLEGAL_ARGUMENT;

    if (!gInputDevice.Initialized)
        return EC_INVALID_STATE;

    HO_STATUS status = KiValidateForegroundCurrentThread();
    if (status != EC_SUCCESS)
        return status;

    KeEnterCriticalSection(&guard);

    if (!gInputDevice.LineReady)
    {
        KeLeaveCriticalSection(&guard);
        return EC_TIMEOUT;
    }

    if (gInputDevice.CompletedLineLength > capacity)
    {
        KeLeaveCriticalSection(&guard);
        return EC_ILLEGAL_ARGUMENT;
    }

    if (gInputDevice.CompletedLineLength != 0U)
        memcpy(buffer, gInputDevice.CompletedLine, gInputDevice.CompletedLineLength);

    *outLength = gInputDevice.CompletedLineLength;
    KeLeaveCriticalSection(&guard);
    return EC_SUCCESS;
}

HO_KERNEL_API HO_STATUS
KeInputConsumeCompletedLineForCurrentThread(void)
{
    KE_CRITICAL_SECTION guard = {0};

    if (!gInputDevice.Initialized)
        return EC_INVALID_STATE;

    HO_STATUS status = KiValidateForegroundCurrentThread();
    if (status != EC_SUCCESS)
        return status;

    KeEnterCriticalSection(&guard);

    if (!gInputDevice.LineReady)
    {
        KeLeaveCriticalSection(&guard);
        return EC_INVALID_STATE;
    }

    gInputDevice.LineReady = FALSE;
    gInputDevice.CompletedLineLength = 0U;
    gInputDevice.CompletedReadCount++;
    memset(gInputDevice.CompletedLine, 0, sizeof(gInputDevice.CompletedLine));
    KeResetEvent(&gInputDevice.LineReadyEvent);
    KeLeaveCriticalSection(&guard);
    return EC_SUCCESS;
}
