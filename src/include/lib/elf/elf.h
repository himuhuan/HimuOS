/**
 * HimuOperatingSystem
 *
 * File: elf.h
 * Description: ELF format definitions & parsing functions
 *
 * Copyright(c) 2024-2025 HimuOS, ONLY FOR EDUCATIONAL PURPOSES.
 */

#pragma once

#include "_hobase.h"

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;

// e_ident values
#define ELFMAG0      0x7F
#define ELFMAG1      'E'
#define ELFMAG2      'L'
#define ELFMAG3      'F'
#define ELFCLASS64   2
#define ELFDATA2LSB  1
#define EV_CURRENT   1

// Machine, type
#define EM_X86_64    62
#define ET_REL       1
#define ET_EXEC      2
#define ET_DYN       3

// Program header types
#define PT_NULL      0
#define PT_LOAD      1
#define PT_DYNAMIC   2
#define PT_INTERP    3
#define PT_NOTE      4
#define PT_SHLIB     5
#define PT_PHDR      6
#define PT_TLS       7
#define PT_GNU_STACK 0x6474e551u

// Program header flags
#define PF_X         0x1
#define PF_W         0x2
#define PF_R         0x4

// e_ident indexes
enum
{
    EI_MAG0 = 0,
    EI_MAG1 = 1,
    EI_MAG2 = 2,
    EI_MAG3 = 3,
    EI_CLASS = 4,
    EI_DATA = 5,
    EI_VERSION = 6,
    EI_OSABI = 7,
    EI_ABIVERSION = 8,
    EI_PAD = 9,
    EI_NIDENT = 16
};

// ELF header
typedef struct Elf64_Ehdr
{
    uint8_t e_ident[EI_NIDENT];
    Elf64_Half e_type;
    Elf64_Half e_machine;
    Elf64_Word e_version;
    Elf64_Addr e_entry;
    Elf64_Off e_phoff;
    Elf64_Off e_shoff;
    Elf64_Word e_flags;
    Elf64_Half e_ehsize;
    Elf64_Half e_phentsize;
    Elf64_Half e_phnum;
    Elf64_Half e_shentsize;
    Elf64_Half e_shnum;
    Elf64_Half e_shstrndx;
} Elf64_Ehdr;

// Program header
typedef struct Elf64_Phdr
{
    Elf64_Word p_type;
    Elf64_Word p_flags;
    Elf64_Off p_offset;
    Elf64_Addr p_vaddr;
    Elf64_Addr p_paddr;
    Elf64_Xword p_filesz;
    Elf64_Xword p_memsz;
    Elf64_Xword p_align;
} Elf64_Phdr;

// clang-format off

// Callback function type for mapping segments, see `Elf64Load` for details
typedef BOOL (*ELF64_MAP_SEGMENT_CALLBACK)(
    HO_VIRTUAL_ADDRESS addrVirt,
    void *seg,
    uint64_t filesiz,
    uint64_t memsiz,
    uint16_t perm,
    uint64_t align);

// clang-format on

HO_NODISCARD HO_KERNEL_API BOOL Elf64Validate(const Elf64_Ehdr *header, uint64_t size);

HO_NODISCARD HO_KERNEL_API const Elf64_Ehdr *Elf64GetHeader(const void *image, uint64_t size);

HO_NODISCARD HO_KERNEL_API uint64_t Elf64GetSegmentPerm(uint32_t phdrFlags);

HO_NODISCARD HO_KERNEL_API const Elf64_Phdr *Elf64GetProgramHeader(const void *image, uint64_t size, uint16_t *outCnt);

typedef struct
{
    HO_VIRTUAL_ADDRESS EntryVirt; // Entry point virtual address
    uint64_t MinAddrVirt;         // Minimum virtual address of all PT_LOAD segments
    uint64_t MaxAddrVirt;         // Maximum virtual address of all PT_LOAD segments
    // Physical memory pages ACTUALLY required to load all PT_LOAD segments which is marked as RX
    uint64_t ExecPhysPages;
    // Physical memory pages ACTUALLY required to load all PT_LOAD segments which is marked as RW
    uint64_t DataPhysPages;
    // Virtual address span of all PT_LOAD segments, may be larger than TotalPhysPages
    uint64_t VirtSpanPages;
} ELF64_LOAD_INFO;

HO_NODISCARD HO_KERNEL_API BOOL Elf64GetLoadInfo(const void *image, uint64_t size, ELF64_LOAD_INFO *outInfo);

/**
 * Elf64Load
 *
 * Load an ELF64 image into memory by mapping its segments using a callback function.
 *
 * @param minbase Minimum virtual address to start loading the image, any segment below this address will be ignored.
 * @param image Pointer to the ELF64 raw image in memory.
 * @param size Size of the ELF64 raw image in bytes.
 * @param mapper Callback function to map each loadable segment.
 * @remarks The callback function must zero out the memory for segments where memsiz > filesiz.
 * @return
 */
HO_NODISCARD HO_KERNEL_API BOOL Elf64Load(HO_VIRTUAL_ADDRESS minbase,
                                          void *image,
                                          uint64_t size,
                                          ELF64_MAP_SEGMENT_CALLBACK mapper);

typedef struct
{
    // The destination memory buffer to load the image into.
    // The buffer must be pre-allocated and large enough to hold all loadable segments.
    void *Base;
    // Size of the output buffer in bytes
    uint64_t BaseSize;
    // Raw ELF image in memory to be loaded
    void *Image;
    // Size of the raw ELF image in bytes
    uint64_t ImageSize;
    // The base virtual address where the buffer is mapped.
    // This is used to calculate the virtual address of each segment.
    HO_VIRTUAL_ADDRESS BaseVirt;
} ELF64_LOAD_BUFFER_PARAMS;

/**
 * Load an ELF64 image into a pre-allocated contiguous buffer.
 * @param params Pointer to ELF64_LOAD_BUFFER_PARAMS structure containing parameters.
 * @remarks This function only supports the specified ELF64 format and does not handle complex scenarios.
 * More details see also documentation of `Bootloader & Boot Protocol` in the HimuOS Developer Guide.
 * @return TRUE if the image is successfully loaded into the buffer, FALSE otherwise.
 */
HO_NODISCARD HO_KERNEL_API BOOL Elf64LoadToBuffer(ELF64_LOAD_BUFFER_PARAMS *params);
