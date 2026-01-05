/**
 * HimuOperatingSystem
 *
 * File: video_efi.h
 * Description:
 * The implementation of the video device for UEFI.
 */

#pragma once

#include "drivers/video_driver.h"

// Get the vtable for UEFI video operations
const VD_VTABLE *VdEfiGetVTable(void);