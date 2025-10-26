#include <drivers/serial.h>
#include <arch/amd64/asm.h>
#include <kernel/console.h>


void
SerialInit(uint16_t port)
{
    outb(SERIAL_INTR_ENABLE_PORT(port), 0x00);
    outb(SERIAL_LINE_CTRL_PORT(port), 0x80);
    outb(SERIAL_DATA_PORT(port), 0x01);
    outb(SERIAL_INTR_ENABLE_PORT(port), 0x00);
    outb(SERIAL_LINE_CTRL_PORT(port), 0x03);
    outb(SERIAL_FIFO_CTRL_PORT(port), 0xC7);
    outb(SERIAL_MODEM_CTRL_PORT(port), 0x0B);
    outb(SERIAL_INTR_ENABLE_PORT(port), 0x00);
}

void
SerialWriteByte(uint16_t port, char byte)
{
    while ((inb(SERIAL_LINE_STATUS_PORT(port)) & 0x20) == 0)
        ;
    outb(SERIAL_DATA_PORT(port), byte);
}

void
SerialWriteStr(const char *s)
{
    while (*s)
    {
        SerialWriteByte(COM1_PORT, *s++);
    }
}
