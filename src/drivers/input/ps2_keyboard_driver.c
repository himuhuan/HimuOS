/**
 * HimuOperatingSystem
 *
 * File: drivers/input/ps2_keyboard_driver.c
 * Description: Minimal QEMU PS/2 keyboard runtime driver primitives.
 * Copyright(c) 2024-2026 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#include <arch/amd64/asm.h>
#include <drivers/input/ps2_keyboard_driver.h>

#define I8042_DATA_PORT            0x60U
#define I8042_STATUS_PORT          0x64U
#define I8042_COMMAND_PORT         0x64U
#define I8042_STATUS_OUTPUT_FULL   0x01U
#define I8042_STATUS_INPUT_FULL    0x02U
#define I8042_COMMAND_READ_CONFIG  0x20U
#define I8042_COMMAND_WRITE_CONFIG 0x60U
#define I8042_COMMAND_ENABLE_PORT1 0xAEU

#define PS2_KEYBOARD_ENABLE_SCANNING 0xF4U

#define PIC1_COMMAND_PORT 0x20U
#define PIC1_DATA_PORT    0x21U
#define PIC2_COMMAND_PORT 0xA0U
#define PIC2_DATA_PORT    0xA1U
#define PIC_EOI           0x20U
#define PIC_ICW1_INIT     0x10U
#define PIC_ICW1_ICW4     0x01U
#define PIC_ICW4_8086     0x01U

#define PS2_KEYBOARD_IRQ_LINE 1U
#define PIC1_REMAP_VECTOR     0x20U
#define PIC2_REMAP_VECTOR     0x28U

static void
Ps2IoWait(void)
{
    outb(0x80U, 0U);
}

static HO_STATUS
Ps2WaitInputBufferReady(void)
{
    for (uint32_t attempt = 0; attempt < 100000U; ++attempt)
    {
        if ((inb(I8042_STATUS_PORT) & I8042_STATUS_INPUT_FULL) == 0)
            return EC_SUCCESS;
    }

    return EC_TIMEOUT;
}

static HO_STATUS
Ps2WaitOutputBufferReady(void)
{
    for (uint32_t attempt = 0; attempt < 100000U; ++attempt)
    {
        if ((inb(I8042_STATUS_PORT) & I8042_STATUS_OUTPUT_FULL) != 0)
            return EC_SUCCESS;
    }

    return EC_TIMEOUT;
}

static HO_STATUS
Ps2WriteCommand(uint8_t command)
{
    HO_STATUS status = Ps2WaitInputBufferReady();
    if (status != EC_SUCCESS)
        return status;

    outb(I8042_COMMAND_PORT, command);
    return EC_SUCCESS;
}

static HO_STATUS
Ps2WriteData(uint8_t value)
{
    HO_STATUS status = Ps2WaitInputBufferReady();
    if (status != EC_SUCCESS)
        return status;

    outb(I8042_DATA_PORT, value);
    return EC_SUCCESS;
}

static HO_STATUS
Ps2ReadData(uint8_t *outValue)
{
    if (outValue == NULL)
        return EC_ILLEGAL_ARGUMENT;

    HO_STATUS status = Ps2WaitOutputBufferReady();
    if (status != EC_SUCCESS)
        return status;

    *outValue = inb(I8042_DATA_PORT);
    return EC_SUCCESS;
}

static void
Ps2DrainOutputBuffer(void)
{
    while ((inb(I8042_STATUS_PORT) & I8042_STATUS_OUTPUT_FULL) != 0)
        (void)inb(I8042_DATA_PORT);
}

static void
PicRemapAndUnmaskKeyboard(PS2_KEYBOARD_DRIVER *driver)
{
    driver->MasterMask = inb(PIC1_DATA_PORT);
    driver->SlaveMask = inb(PIC2_DATA_PORT);

    outb(PIC1_COMMAND_PORT, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    Ps2IoWait();
    outb(PIC2_COMMAND_PORT, PIC_ICW1_INIT | PIC_ICW1_ICW4);
    Ps2IoWait();

    outb(PIC1_DATA_PORT, PIC1_REMAP_VECTOR);
    Ps2IoWait();
    outb(PIC2_DATA_PORT, PIC2_REMAP_VECTOR);
    Ps2IoWait();

    outb(PIC1_DATA_PORT, 0x04U);
    Ps2IoWait();
    outb(PIC2_DATA_PORT, 0x02U);
    Ps2IoWait();

    outb(PIC1_DATA_PORT, PIC_ICW4_8086);
    Ps2IoWait();
    outb(PIC2_DATA_PORT, PIC_ICW4_8086);
    Ps2IoWait();

    outb(PIC1_DATA_PORT, (uint8_t)(driver->MasterMask & (uint8_t)~(1U << PS2_KEYBOARD_IRQ_LINE)));
    Ps2IoWait();
    outb(PIC2_DATA_PORT, driver->SlaveMask);
    Ps2IoWait();
}

HO_KERNEL_API HO_STATUS
Ps2KeyboardDriverInit(PS2_KEYBOARD_DRIVER *driver)
{
    if (driver == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (driver->Initialized)
        return EC_SUCCESS;

    Ps2DrainOutputBuffer();

    HO_STATUS status = Ps2WriteCommand(I8042_COMMAND_ENABLE_PORT1);
    if (status != EC_SUCCESS)
        return status;

    uint8_t config = 0;
    status = Ps2WriteCommand(I8042_COMMAND_READ_CONFIG);
    if (status != EC_SUCCESS)
        return status;

    status = Ps2ReadData(&config);
    if (status != EC_SUCCESS)
        return status;

    config |= 0x01U;
    config |= 0x40U;

    status = Ps2WriteCommand(I8042_COMMAND_WRITE_CONFIG);
    if (status != EC_SUCCESS)
        return status;

    status = Ps2WriteData(config);
    if (status != EC_SUCCESS)
        return status;

    status = Ps2WriteData(PS2_KEYBOARD_ENABLE_SCANNING);
    if (status != EC_SUCCESS)
        return status;

    PicRemapAndUnmaskKeyboard(driver);
    Ps2DrainOutputBuffer();

    driver->Initialized = TRUE;
    return EC_SUCCESS;
}

HO_KERNEL_API BOOL
Ps2KeyboardDriverHasPendingData(const PS2_KEYBOARD_DRIVER *driver)
{
    return driver != NULL && driver->Initialized && ((inb(I8042_STATUS_PORT) & I8042_STATUS_OUTPUT_FULL) != 0);
}

HO_KERNEL_API HO_STATUS
Ps2KeyboardDriverReadScanCode(const PS2_KEYBOARD_DRIVER *driver, uint8_t *outScanCode)
{
    if (driver == NULL || outScanCode == NULL)
        return EC_ILLEGAL_ARGUMENT;

    if (!driver->Initialized)
        return EC_INVALID_STATE;

    if (!Ps2KeyboardDriverHasPendingData(driver))
        return EC_INVALID_STATE;

    *outScanCode = inb(I8042_DATA_PORT);
    return EC_SUCCESS;
}

HO_KERNEL_API void
Ps2KeyboardDriverAcknowledgeInterrupt(const PS2_KEYBOARD_DRIVER *driver)
{
    if (driver == NULL || !driver->Initialized)
        return;

    outb(PIC1_COMMAND_PORT, PIC_EOI);
}
