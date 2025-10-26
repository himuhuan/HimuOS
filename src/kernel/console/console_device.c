#include "console_device.h"
#include <drivers/basic_color.h>

HO_KERNEL_API void
ConDevInit(CONSOLE_DEVICE *dev, struct CONSOLE_SINK *sink)
{
    dev->CursorX = 0;
    dev->CursorY = 0;
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