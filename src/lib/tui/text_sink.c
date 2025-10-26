#include <lib/tui/tui.h>

static void TrScrollDown(TEXT_RENDER_SINK *self, uint8_t count, uint32_t maxScreenWidth);

HO_KERNEL_API void
TrSinkInit(const char *name, TEXT_RENDER_SINK *sink, TEXT_RENDERER *renderer)
{
    memset(sink, 0, sizeof(TEXT_RENDER_SINK));
    //sink->Base.HorizontalResolution = renderer->Device->HorizontalResolution;
    strcpy(sink->Base.Name, name);
    sink->Base.PutChar = TrSinkPutChar;
    sink->Base.Cls = TrSinkClearScreen;
    sink->Renderer = renderer;
    sink->ForegroundColor = COLOR_TEXT;
    sink->BackgroundColor = COLOR_BLACK;
}

int
TrSinkPutChar(CONSOLE_SINK *sink, int c)
{
    TR_RENDER_CHAR_PARAMS param;
    int code;
    BOOL move = TRUE; // Move cursor after writing character
    TEXT_RENDER_SINK *this = (TEXT_RENDER_SINK *)sink;
    uint8_t charWidth = this->Renderer->Font->Width, charHeight = this->Renderer->Font->Height;
    const uint16_t kScale = 1;     // TODO: Make this configurable
    const uint8_t kCharPerTab = 8; // TODO: Make this configurable
    const uint32_t kCharWidthScaled = charWidth * kScale;
    const uint32_t kMaxScreenWidth = this->Renderer->Device->HorizontalResolution;
    const uint32_t kMaxScreenHeight = this->Renderer->Device->VerticalResolution;

    memset(&param, 0, sizeof(TR_RENDER_CHAR_PARAMS));
    // Printable Characters
    if (c >= ' ' && c <= '~')
    {
    render_char:
        param.Scale = kScale;
        param.Char = c;
        param.X = this->CursorX;
        param.Y = this->CursorY;
        param.TextColor = this->ForegroundColor;
        param.BackgroundColor = this->BackgroundColor;
        code = TrRenderChar(this->Renderer, &param);
        if (code != EC_SUCCESS)
            return code;
        this->CursorX += kCharWidthScaled * move;
        goto update_cursor;
    }

    // Control Characters
    switch (c)
    {
    case '\b':
        if (this->CursorX >= kCharWidthScaled)
        {
            this->CursorX -= kCharWidthScaled;
            c = ' ';
            move = FALSE;
            goto render_char;
        }
        break;
    case '\r':
        this->CursorX = 0;
        break;
    case '\t': {
        uint32_t current = this->CursorX / kCharWidthScaled;
        uint32_t nextStop = (current / kCharPerTab + 1) * kCharPerTab;
        this->CursorX = nextStop * kCharWidthScaled;
        break;
    }
    case '\n':
        this->CursorX = 0;
        this->CursorY += charHeight * kScale;
        break;
    default:
        goto render_char;
    }

update_cursor:
    // Update Cursor Position
    if (this->CursorX >= kMaxScreenWidth)
    {
        this->CursorX = 0;
        this->CursorY += charHeight * kScale;
    }

    if (this->CursorY >= kMaxScreenHeight)
    {
        TrScrollDown(this, 1, kMaxScreenWidth);
    }

    return EC_SUCCESS;
}

void
TrSinkClearScreen(CONSOLE_SINK *sink, COLOR32 color)
{
    TEXT_RENDER_SINK *this = (TEXT_RENDER_SINK *)sink;
    VdClearScreen(this->Renderer->Device, color);
}

static void
TrScrollDown(TEXT_RENDER_SINK *self, uint8_t count, uint32_t maxScreenWidth)
{
    const uint8_t kScale = 1; // TODO: Make this configurable
    const size_t kLineSize = maxScreenWidth * self->Renderer->Font->Height * kScale * 4;
    const size_t kScrollSize = kLineSize * count;
    uint8_t *dest = (uint8_t *)self->Renderer->Device->FrameBuffer;
    uint8_t *src = dest + kScrollSize;
    memmove(dest, src, self->Renderer->Device->FrameBufferSize - kScrollSize);
    memset(dest + (self->Renderer->Device->FrameBufferSize - kScrollSize), 0, kScrollSize);
    self->CursorY -= self->Renderer->Font->Height * kScale * count;
}

HO_KERNEL_API void
TrSinkSetAlign(TEXT_RENDER_SINK *sink, uint32_t len, TR_PUTS_ALIGNMENT align)
{
    TEXT_RENDER_SINK *this = sink;
    uint8_t charWidth = this->Renderer->Font->Width;
    const uint8_t kScale = 1; // TODO: Make this configurable
    const uint32_t kCharWidthScaled = charWidth * kScale;
    uint32_t totalStrWidth = len * kCharWidthScaled;
    uint32_t hr = this->Renderer->Device->HorizontalResolution;

    // Calculate starting X position based on alignment
    switch (align)
    {
    case TR_PUTS_ALIGN_LEFT:
        // No change needed
        break;
    case TR_PUTS_ALIGN_CENTER:
        if (totalStrWidth < hr)
            this->CursorX = (hr - totalStrWidth) / 2;
        else
            this->CursorX = 0; // If string is wider than screen, start at 0
        break;
    case TR_PUTS_ALIGN_RIGHT:
        if (totalStrWidth < hr)
            this->CursorX = hr - totalStrWidth;
        else
            this->CursorX = 0;
        break;
    default:
        break;
    }
}

HO_KERNEL_API void
TrSinkGetColor(TEXT_RENDER_SINK *sink, COLOR32 *fg, COLOR32 *bg)
{

    if (fg)
        *fg = sink->ForegroundColor;
    if (bg)
        *bg = sink->BackgroundColor;
}

HO_KERNEL_API void
TrSinkSetColor(TEXT_RENDER_SINK *sink, COLOR32 fg, COLOR32 bg)
{
    sink->ForegroundColor = fg;
    sink->BackgroundColor = bg;
}