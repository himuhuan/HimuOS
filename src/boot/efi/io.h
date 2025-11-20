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

EFI_STATUS ConsoleFormatWrite(const CHAR16 *fmt, ...);
