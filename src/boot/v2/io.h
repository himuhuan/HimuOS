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

#include "arch/amd64/efi_min.h"
#include "boot/boot_capsule.h"

/**
 * @brief Reads a line of input from the console.
 * @param Buffer Pointer to the buffer to store the input.
 * @param MaxCount Maximum number of characters to read.
 * @return The # of characters read, or -1 on error.
 */
MAYBE_UNUSED INT64 ConsoleReadline(OUT CHAR16 *Buffer, IN INT64 MaxCount);

/**
 * @brief Writes a string to the console.
 * @param Buffer Pointer to the string to write.
 */
void ConsoleWriteStr(IN const CHAR16 *Buffer);

EFI_STATUS ConsoleFormatWrite(const CHAR16 *fmt, ...);
void PrintCapsule(const BOOT_CAPSULE *capsule, const CHAR16 *tag);

#define LOG_DEBUG(fmt, ...)   ConsoleFormatWrite(L"[DEBUG  ] " fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)    ConsoleFormatWrite(L"[INFO   ] " fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) ConsoleFormatWrite(L"[WARNING] " fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...)   ConsoleFormatWrite(L"[ERROR  ] " fmt, ##__VA_ARGS__)
#define PRINT_CAPSULE(capsule_ptr) PrintCapsule((capsule_ptr), TEXT(#capsule_ptr))

EFI_STATUS GetFileSize(EFI_FILE_PROTOCOL *file, UINT64 *outSize);

EFI_STATUS ReadKernelImage(IN const CHAR16 *path, OUT void **outImage, OUT UINT64 *outSize);
