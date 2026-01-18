/**
 * HimuOperatingSystem
 *
 * File: video_driver.h
 * Description:
 * When the kernel is booted in UEFI mode, the `FrameBuffer` will point to the buffer
 * provided by the UEFI firmware. In legacy mode, it may point to a different buffer
 * depending on the video hardware and BIOS settings.
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"
#include "basic_color.h"
#include "arch/amd64/efi_min.h"
#include "boot/boot_capsule.h"

typedef struct
{
    uint32_t X, Y;
    uint32_t Width, Height;
    uint32_t Color;
    BOOL Filled;
} VD_RENDER_RECT_PARAMS;

struct _VIDEO_DRIVER;

typedef struct
{
    uint32_t(HO_NODISCARD *ToPhysColor)(struct _VIDEO_DRIVER *self, COLOR32 color);
    HO_STATUS(HO_NODISCARD *ClearScreen)(struct _VIDEO_DRIVER *self, COLOR32 color);
    HO_STATUS(HO_NODISCARD *RenderPixel)(struct _VIDEO_DRIVER *self, uint32_t x, uint32_t y, COLOR32 color);
    HO_STATUS(HO_NODISCARD *RenderRect)(struct _VIDEO_DRIVER *self, VD_RENDER_RECT_PARAMS *params);
} VD_VTABLE;

/**
 * _VIDEO_DEVICE: Represents a basic video device context.
 *
 * @remarks This structure is used to define the properties and state of a video device
 * within the operating system. It typically contains information required for
 * video output operations and device management.
 */
typedef struct _VIDEO_DRIVER
{
    enum VIDEO_MODE_TYPE Type;     // Type of video mode (UEFI or Legacy)
    enum PIXEL_FORMAT Format;      // Pixel format (RGB or BGR)
    uint32_t HorizontalResolution; // Horizontal resolution in pixels
    uint32_t VerticalResolution;   // Vertical resolution in pixels
    uint32_t PixelsPerScanLine;    // Number of pixels per scan line
    void *FrameBuffer;
    uint64_t FrameBufferSize;  // Size of the framebuffer in bytes
    const VD_VTABLE *Methods;
} KE_VIDEO_DRIVER;

/**
 * @brief Initialize the video device context.
 *
 * Copy the video information from the boot info header to the video device structure
 * and set up the current video device context.
 *
 * @param pd Video device to initialize
 * @param info Boot information header
 */
void HO_KERNEL_API VdInit(KE_VIDEO_DRIVER *pd, STAGING_BLOCK *info);

/**
 * @brief Renders a single pixel on the specified video device.
 *
 * This function sets the color of the pixel at the given (x, y) coordinates
 * on the provided VIDEO_DEVICE to the specified COLOR32 value.
 *
 * @param device Pointer to the VIDEO_DEVICE structure where the pixel will be rendered.
 * @param x The x-coordinate of the pixel to render.
 * @param y The y-coordinate of the pixel to render.
 * @param color The COLOR32 value to set for the pixel.
 * @return HO_STATUS indicating the success or failure of the operation.
 */
HO_STATUS HO_KERNEL_API VdRenderPixel(KE_VIDEO_DRIVER *device, uint32_t x, uint32_t y, COLOR32 color);

/**
 * @brief Render a rectangle on the video device.
 *
 * This function draws a rectangle on the specified video device using the provided parameters.
 *
 * @param device Pointer to the video device where the rectangle will be rendered.
 * @param params Pointer to the parameters defining the rectangle's properties (position, size, color, etc.).
 * @return HO_STATUS indicating success or failure of the operation.
 */
HO_STATUS HO_KERNEL_API VdRenderRect(KE_VIDEO_DRIVER *device, VD_RENDER_RECT_PARAMS *params);

/**
 * @brief Clear the entire screen of the video device with a specified color.
 *
 * This function fills the entire screen of the given video device with the specified color.
 *
 * @param device Pointer to the video device to be cleared.
 * @param color The color to fill the screen with, represented as a 32-bit unsigned integer.
 * @return HO_STATUS indicating success or failure of the operation.
 */
HO_STATUS HO_KERNEL_API VdClearScreen(KE_VIDEO_DRIVER *device, uint32_t color);
