/**
 * HimuOperatingSystem
 *
 * File: io.h
 * Description: Utilities for UEFI I/O operations
 * Module: boot
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "efi.h"

/**
 * @brief Reads a line of input from the console.
 * @param Buffer Pointer to the buffer to store the input.
 * @param MaxCount Maximum number of characters to read.
 * @return The # of characters read, or -1 on error.
 */
INT64 ConsoleReadline(OUT CHAR16 *Buffer, IN INT64 MaxCount);

/**
 * @brief Writes a string to the console.
 * @param Buffer Pointer to the string to write.
 */
void ConsoleWriteStr(IN const CHAR16 *Buffer);

/**
 * @brief Writes a 64-bit hexadecimal value to the console output.
 *
 * This function outputs the given 64-bit unsigned integer value in hexadecimal format
 * to the console. It is typically used for debugging or displaying memory addresses.
 *
 * @param value The 64-bit unsigned integer value to be written in hexadecimal format.
 */
void ConsoleWriteHex(uint64_t value);

void ConsoleWriteUInt64(uint64_t value);

#define PRINT_HEX_WITH_MESSAGE(message, value)                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        ConsoleWriteStr(TEXT(message));                                                                                \
        ConsoleWriteHex(value);                                                                                        \
        ConsoleWriteStr(TEXT("\r\n"));                                                                                 \
    } while (0)

void PrintFormatAddressRange(IN const CHAR16 *Msg, IN uint64_t Begin, IN uint64_t Size);

void FormatHex(uint64_t value, CHAR16 *buffer);
void FormatUInt64(uint64_t value, CHAR16 *buffer);
void FormatAsStorageUnit(uint64_t Size, CHAR16 *buffer);
int CopyString(CHAR16 *dest, const CHAR16 *src);

void PrintFormatStorageSize(IN uint64_t Size);

#define PRINT_SIZ_WITH_MESSAGE(message, value)                                                                         \
    do                                                                                                                 \
    {                                                                                                                  \
        ConsoleWriteStr(TEXT(message));                                                                                \
        PrintFormatStorageSize(value);                                                                                 \
        ConsoleWriteStr(TEXT("\r\n"));                                                                                 \
    } while (0)

#define PRINT_ADDR_RANGE_WITH_MESSAGE(message, begin, size) PrintFormatAddressRange(TEXT(message), begin, size)

#define PRINT_ADDR_MAP(message, phys, virt, size)                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        ConsoleWriteStr(TEXT(message));                                                                                \
        ConsoleWriteStr(TEXT(" "));                                                                                    \
        ConsoleWriteHex(phys);                                                                                         \
        ConsoleWriteStr(TEXT(" -> "));                                                                                 \
        ConsoleWriteHex(virt);                                                                                         \
        ConsoleWriteStr(TEXT(": "));                                                                                   \
        PrintFormatStorageSize(size);                                                                                  \
        ConsoleWriteStr(TEXT("\r\n"));                                                                                 \
    } while (0)
