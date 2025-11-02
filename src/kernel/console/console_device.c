#include "console_device.h"
#include <drivers/basic_color.h>

enum
{
    STATE_NORMAL = 0,
    STATE_GOT_ESC, // \x1B
    STATE_GOT_CSI  // \x1B[
};

// If the character is part of an ANSI escape sequence, process it and return TRUE.
static BOOL TryProcessAnisSeq(CONSOLE_DEVICE *dev, char ch);

// Map ANSI code to console device attributes.
static void ApplyAnsiCode(CONSOLE_DEVICE *dev, int code);

HO_KERNEL_API void
ConDevInit(CONSOLE_DEVICE *dev, struct CONSOLE_SINK *sink)
{
    memset(dev, 0, sizeof(CONSOLE_DEVICE));
    dev->Foreground = COLOR_TEXT;
    dev->Background = COLOR_BLACK;
    dev->Sink = sink;
}

HO_KERNEL_API int
ConDevPutChar(CONSOLE_DEVICE *this, char c)
{
    CONSOLE_SINK_INFO info;
    if (!this || !this->Sink || this->Sink->GetInfo(this->Sink, &info) != EC_SUCCESS)
        return EC_ILLEGAL_ARGUMENT;

    if (TryProcessAnisSeq(this, c))
        return EC_SUCCESS;

    BOOL move = TRUE;
    const uint16_t kGridWidth = info.GridWidth;
    const uint16_t kGridHeight = info.GridHeight;
    const uint8_t kCharPerTab = info.CharPerTab;

    if (HO_LIKELY(c >= ' ' && c <= '~'))
    {
    render_char:
        this->Sink->PutChar(this->Sink, this->CursorX, this->CursorY, c, this->Foreground, this->Background);
        if (move)
            this->CursorX++;
    }
    else
    {
        switch (c)
        {
        case '\b':
            if (this->CursorX > 0)
            {
                this->CursorX--;
                c = ' ';
                move = FALSE;
                goto render_char;
            }
            break;
        case '\r':
            this->CursorX = 0;
            break;
        case '\t':
            this->CursorX = (this->CursorX / kCharPerTab + 1) * kCharPerTab;
            break;
        case '\n':
            this->CursorX = 0;
            this->CursorY++;
            break;
        default:
            goto render_char;
        }
    }

    if (this->CursorX >= kGridWidth)
    {
        this->CursorX = 0;
        this->CursorY++;
    }

    if (this->CursorY >= kGridHeight)
    {
        this->Sink->Scroll(this->Sink, 1, this->Background);
        this->CursorY = kGridHeight - 1;
    }
    return EC_SUCCESS;
}

HO_KERNEL_API uint64_t
ConDevPutStr(CONSOLE_DEVICE *this, const char *str)
{
    uint64_t written = 0;
    const char *p;

    for (p = str; *p; ++p)
    {
        int code = ConDevPutChar(this, *p);
        if (code != EC_SUCCESS)
            break;
        ++written;
    }

    return written;
}

HO_KERNEL_API void
ConDevClearScreen(CONSOLE_DEVICE *dev, COLOR32 color)
{
    if (!dev || !dev->Sink || !dev->Sink->Clear)
        return;

    dev->Sink->Clear(dev->Sink, color);
    dev->CursorX = 0;
    dev->CursorY = 0;
}

static BOOL
TryProcessAnisSeq(CONSOLE_DEVICE *dev, char ch)
{
    switch (dev->_ParserState.EscSeqState)
    {
    case STATE_NORMAL:
        if (ch == 0x1B)
        { // ESC
            dev->_ParserState.EscSeqState = STATE_GOT_ESC;
            return TRUE;
        }
        break;
    case STATE_GOT_ESC:
        if (ch == '[')
        { // CSI
            dev->_ParserState.EscSeqState = STATE_GOT_CSI;
            dev->_ParserState.AnsiHasCode = FALSE;
            dev->_ParserState.AnsiCurrentCode = 0;
            return TRUE;
        }
        else
        {
            dev->_ParserState.EscSeqState = STATE_NORMAL;
            // Not a valid sequence, ignore
            // NOTE: In this simple implementation, we dropped the ESC.
            return FALSE;
        }
    case STATE_GOT_CSI:
        if (ch >= '0' && ch <= '9')
        {
            dev->_ParserState.AnsiCurrentCode = dev->_ParserState.AnsiCurrentCode * 10 + (ch - '0');
            dev->_ParserState.AnsiHasCode = TRUE;
            return TRUE;
        }
        else if (ch == ';')
        {
            ApplyAnsiCode(dev, dev->_ParserState.AnsiCurrentCode);
            dev->_ParserState.AnsiCurrentCode = 0;
            dev->_ParserState.AnsiHasCode = FALSE;
            return TRUE;
        }
        else if (ch == 'm')
        {
            int code = dev->_ParserState.AnsiHasCode ? dev->_ParserState.AnsiCurrentCode : 0;
            ApplyAnsiCode(dev, code);
            dev->_ParserState.EscSeqState = STATE_NORMAL;
            return TRUE;
        }
        else
        {
            // Unsupported sequence, reset state
            dev->_ParserState.EscSeqState = STATE_NORMAL;
            return TRUE; // "eat" the unsupported sequence
        }
        break;
    }
    return FALSE;
}

static void
ApplyAnsiCode(CONSOLE_DEVICE *dev, int code)
{
    static const COLOR32 kForeColors[8] = {COLOR_BLACK, COLOR_RED,     COLOR_GREEN, COLOR_YELLOW,
                                           COLOR_BLUE,  COLOR_MAGENTA, COLOR_CYAN,  COLOR_WHITE};
    static const COLOR32 kBackColors[8] = {COLOR_BLACK, COLOR_RED,     COLOR_GREEN, COLOR_YELLOW,
                                           COLOR_BLUE,  COLOR_MAGENTA, COLOR_CYAN,  COLOR_WHITE};

    if (code == 0)
    {
        // Reset
        dev->Foreground = COLOR_TEXT;
        dev->Background = COLOR_BLACK;
    }
    else if (code >= 30 && code <= 37)
    {
        dev->Foreground = kForeColors[code - 30];
    }
    else if (code >= 40 && code <= 47)
    {
        dev->Background = kBackColors[code - 40];
    }
}