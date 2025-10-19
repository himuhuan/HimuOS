/**
 * HimuOperatingSystem
 *
 * File: serial.h
 * Description:
 * Generic serial port driver header file.
 *
 * NOTE: Only x86-64 architecture is supported.
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

#define PORT_COM1                     0x3F8

#define SERIAL_DATA_PORT(base)        (base)
#define SERIAL_INTR_ENABLE_PORT(base) (base + 1)
#define SERIAL_FIFO_CTRL_PORT(base)   (base + 2)
#define SERIAL_LINE_CTRL_PORT(base)   (base + 3)
#define SERIAL_MODEM_CTRL_PORT(base)  (base + 4)
#define SERIAL_LINE_STATUS_PORT(base) (base + 5)

void SerialInit(uint16_t port);

void SerialWriteByte(uint16_t port, char byte);

void SerialWriteStr(const char *s);
