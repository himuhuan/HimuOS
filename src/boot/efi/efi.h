// HimuOperatingSystem
// File: efi.h
// Description: The base defination of efi.
// Created: 2025-07-25 00:29:06 LiuHuan
// @Last Modified by: LiuHuan
// @Last Modified time: 2025-07-25 00:29:22
// Copyright(c) 2024-2025 HimuOperatingSystem, all rights reserved.

#pragma once

#ifndef EFI_H
#define EFI_H

#include "libc/_stdbase.h"
#include "kernel/mm/mm_efi.h"

typedef unsigned char BYTE;

typedef unsigned short CHAR16;

typedef unsigned long long UINT64;

typedef uint32_t UINT32;

typedef uint16_t UINT16;

typedef int16_t INT16;

typedef UINT64 UINTN;

typedef unsigned char UINT8;

typedef long long INT64;

// NOTE: EFI stanard tells us the BOOL only has 1 byte size, but 'BOOL' typedefs in Windows is 4 bytes.
typedef BYTE BOOL;

typedef unsigned long long EFI_STATUS;

typedef void *EFI_EVENT;
typedef void *EFI_HANDLE;

#define TEXT(s) L##s

#define EFIAPI
#define IN
#define OUT

#define TRUE          1
#define FALSE         0

#define EFI_PAGE_SIZE 4096

enum
{
    EFI_SUCCESS = 0,           /* The operation completed successfully. */
    EFI_LOAD_ERROR = 1,        /* The image failed to load. */
    EFI_INVALID_PARAMETER = 2, /* A parameter was incorrect. */
    EFI_UNSUPPORTED = 3,       /* The operation is not supported. */
    EFI_BUFFER_TOO_SMALL = 5,  /* The buffer was not large enough to hold the requested data. */
    EFI_OUT_OF_RESOURCES = 10, /* A resource has run out. */
    EFI_NOT_FOUND = 14         /* The item was not found. */
};

#define EFI_ERROR(code) ((code) != EFI_SUCCESS)

HO_NODISCARD UINT64 GetStatusCodeLow(UINT64 code);

/**
 * STRUCT_RESERVED
 * @brief Defines a macro to declare a reserved byte array within a structure.
 *
 * This macro is typically used to reserve a specified number of bytes in a struct,
 * often for alignment or future use, without assigning them a specific purpose.
 *
 * @param i The unique index to differentiate multiple reserved fields.
 * @param n The number of bytes to reserve.
 */
#define STRUCT_RESERVED(i, n) BYTE __reserved_bytes##i[n];

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
{
    STRUCT_RESERVED(1, 8);
    EFI_STATUS (*OutputString)(IN struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN CHAR16 *String);
    STRUCT_RESERVED(2, 32);
    EFI_STATUS (*ClearScreen)(IN struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
    EFI_STATUS (*SetCursorPosition)(IN struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN UINTN Column, IN UINTN Row);
};

struct EFI_INPUT_KEY
{
    CHAR16 ScanCode;
    CHAR16 UnicodeChar;
};

struct EFI_GUID
{
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    BYTE Data4[8];
};

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL
{
    STRUCT_RESERVED(1, 8);
    EFI_STATUS (*ReadKeyStroke)(IN struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, OUT struct EFI_INPUT_KEY *Key);
    EFI_EVENT WaitForKey;
};

enum EFI_ALLOCATE_TYPE
{
    AllocateAnyPages = 0,
    AllocateMaxAddress = 1,
    AllocateAddress = 2,
    MaxAllocateType
};

struct EFI_BOOT_SERVICES
{
    STRUCT_RESERVED(1, 24);
    // Task Priority Services
    STRUCT_RESERVED(2, 16);
    // Memory Services
    EFI_STATUS(*AllocatePages)
    (IN enum EFI_ALLOCATE_TYPE Type,
     IN EFI_MEMORY_TYPE MemoryType,
     IN UINTN Pages,
     IN OUT EFI_PHYSICAL_ADDRESS *Memory);
    EFI_STATUS (*FreePages)(IN EFI_PHYSICAL_ADDRESS Memory, IN UINTN Pages);
    EFI_STATUS(*GetMemoryMap)
    (IN OUT UINTN *MemoryMapSize,
     IN OUT EFI_MEMORY_DESCRIPTOR *MemoryMap,
     OUT UINTN *MapKey,
     OUT UINTN *DescriptorSize,
     OUT UINT32 *DescriptorVersion);
    EFI_STATUS (*AllocatePool)(IN EFI_MEMORY_TYPE PoolType, IN UINTN Size, OUT void **Buffer);
    EFI_STATUS (*FreePool)(IN void *Buffer);
    // Event & Timer Services
    STRUCT_RESERVED(4, 16);
    UINT64 (*WaitForEvent)(UINT64 NumberOfEvents, void **Event, UINT64 *Index);
    STRUCT_RESERVED(5, 24);
    // Protocol Handler Services
    STRUCT_RESERVED(6, 72);
    // Image Services
    STRUCT_RESERVED(7, 32);
    EFI_STATUS (*ExitBootServices)(IN EFI_HANDLE ImageHandle, IN UINTN MapKey);
    // Miscellaneous Services
    STRUCT_RESERVED(8, 16);
    UINT64(*SetWatchdogTimer)
    (IN UINT64 Timeout, IN UINT64 WatchdogCode, IN UINT64 DataSize, OUT unsigned short *WatchdogData);
    // DriverSupport Services
    STRUCT_RESERVED(9, 16);
    // Open and Close Protocol Services
    STRUCT_RESERVED(10, 24);
    // Library Services
    STRUCT_RESERVED(11, 16);
    EFI_STATUS (*LocateProtocol)(struct EFI_GUID *Protocol, void *Registration, void **Interface);
    STRUCT_RESERVED(12, 16);

    // 32-bit CRC Services
    STRUCT_RESERVED(13, 8);
    // Miscellaneous Services
    STRUCT_RESERVED(14, 24);
};

enum EFI_GRAPHICS_PIXEL_FORMAT
{
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
};

typedef struct
{
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION
{
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    enum EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE
{
    UINT32 MaxMode;
    UINT32 Mode;
    struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINT64 SizeOfInfo;
    UINT64 FrameBufferBase;
    UINT64 FrameBufferSize;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL
{
    STRUCT_RESERVED(1, 24);
    struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

struct EFI_GRAPHICS_OUTPUT_BLT_PIXEL
{
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
};

typedef struct _EFI_FILE_PROTOCOL
{
    UINT64 Revision;
    EFI_STATUS(*Open)
    (struct _EFI_FILE_PROTOCOL *This,
     struct _EFI_FILE_PROTOCOL **NewHandle,
     CHAR16 *FileName,
     UINT64 OpenMode,
     UINT64 Attributes);
    EFI_STATUS (*Close)(struct _EFI_FILE_PROTOCOL *This);
    STRUCT_RESERVED(2, 8);
    EFI_STATUS (*Read)(struct _EFI_FILE_PROTOCOL *This, UINT64 *BufferSize, void *Buffer);
    EFI_STATUS (*Write)(struct _EFI_FILE_PROTOCOL *This, UINT64 *BufferSize, void *Buffer);
    EFI_STATUS (*GetPosition)(struct _EFI_FILE_PROTOCOL *This, UINT64 *Position);
    EFI_STATUS (*SetPosition)(struct _EFI_FILE_PROTOCOL *This, UINT64 Position);
    EFI_STATUS (*GetInfo)
    (struct _EFI_FILE_PROTOCOL *This, struct EFI_GUID *InformationType, UINT64 *BufferSize, void *Buffer);
    EFI_STATUS (*SetInfo)
    (struct _EFI_FILE_PROTOCOL *This, struct EFI_GUID *InformationType, UINT64 BufferSize, void *Buffer);
    EFI_STATUS (*Flush)(struct _EFI_FILE_PROTOCOL *This);
    STRUCT_RESERVED(4, 8);
} EFI_FILE_PROTOCOL;

typedef struct
{
    UINT16 Year;
    UINT8 Month;
    UINT8 Day;
    UINT8 Hour;
    UINT8 Minute;
    UINT8 Second;
    UINT8 Pad1;
    UINT32 Nanosecond;
    INT16 TimeZone;
    UINT8 Daylight;
    UINT8 Pad2;
} EFI_TIME;

//******************************************
// File Attribute Bits
//******************************************

#define EFI_FILE_READ_ONLY   0x0000000000000001
#define EFI_FILE_HIDDEN      0x0000000000000002
#define EFI_FILE_SYSTEM      0x0000000000000004
#define EFI_FILE_RESERVED    0x0000000000000008
#define EFI_FILE_DIRECTORY   0x0000000000000010
#define EFI_FILE_ARCHIVE     0x0000000000000020
#define EFI_FILE_VALID_ATTR  0x0000000000000037

#define EFI_FILE_MODE_READ   0x0000000000000001
#define EFI_FILE_MODE_WRITE  0x0000000000000002
#define EFI_FILE_MODE_CREATE 0x8000000000000000

#define MAX_FILEINFO_SIZ     1024

// EFI File Info GUID: 09576e92-6d3f-11d2-8e39-00a0c969723b
#define EFI_FILE_INFO_GUID                                                                                             \
    {                                                                                                                  \
        0x09576e92, 0x6d3f, 0x11d2,                                                                                    \
        {                                                                                                              \
            0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b                                                             \
        }                                                                                                              \
    }

#define OFFSET_OF(s, m) (size_t)((char *)(&((s *)0)->m))

typedef struct
{
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[];
} EFI_FILE_INFO;

typedef struct
{
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    UINT64 Attribute;
    CHAR16 FileName[MAX_FILEINFO_SIZ];
} FILE_INFO;

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
{
    UINT64 Revision;
    EFI_STATUS (*OpenVolume)(struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, struct _EFI_FILE_PROTOCOL **Root);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

struct EFI_SYSTEM_TABLE
{
    STRUCT_RESERVED(1, 44);
    struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConsoleInput;
    STRUCT_RESERVED(2, 8);
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConsoleOutput;
    STRUCT_RESERVED(3, 24);
    struct EFI_BOOT_SERVICES *BootServices;
};

extern struct EFI_SYSTEM_TABLE *g_ST;
extern struct EFI_GRAPHICS_OUTPUT_PROTOCOL *g_GOP;
extern struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *g_FSP;
extern EFI_HANDLE gImageHandle;

void EfiInitialize(EFI_HANDLE imageHandle, struct EFI_SYSTEM_TABLE *SystemTable);

// HO_NODISCARD HO_KERNEL_API int ScanMemoryMap(IN_OUT MM_INITIAL_MAP *map, uint64_t stagingReserved);

HO_NODISCARD HO_KERNEL_API int ListDir(IN CHAR16 *dir,
                                       OUT FILE_INFO *files,
                                       IN uint64_t maxFiles,
                                       OUT uint64_t *fileCount);

EFI_STATUS FillMemoryMap(IN_OUT MM_INITIAL_MAP *map);
MM_INITIAL_MAP *GetLoaderRuntimeMemoryMap();

#endif // EFI_H
