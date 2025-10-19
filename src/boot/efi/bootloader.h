/**
 * HimuOperatingSystem
 *
 * File: bootloader.h
 * Description: This file contains the definition of the bootloader interface.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "efi.h"
#include "../boot.h"

/**
 * @brief Staging the kernel and transferring control to it.
 * @param path The path to the kernel file.
 * @return This function only returns if an error occurs.
 */
HO_KERNEL_API EFI_STATUS StagingKernel(const CHAR16 *path);

