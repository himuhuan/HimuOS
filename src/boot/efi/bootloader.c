#include "bootloader.h"
#include "alloc.h"
#include "io.h"
#include "common/elf/elf.h"
#include "arch/amd64/pm.h"

// typedefs

typedef struct
{
    HO_PHYSICAL_ADDRESS StagingBlockPhys; // Physical address for staging block
    UINT64 TotalReclaimableMem;           // Total memory that can be reclaimed after loading
} SCAN_MEMMAP_RESULT;

typedef struct
{
    UINT64 HeaderPages;     // Pages used for packed memory map and staging block
    UINT64 KernelCodePages; // Pages used for kernel executable segments in physical memory
    UINT64 KernelDataPages; // Pages used for kernel data segments (including BSS) in physical memory
    UINT64 KernelSpanPages; // Pages spanned by the kernel in virtual memory
    UINT64 PageTablePages;  // Pages used for page tables
    UINT64 TotalPages;      // Total pages required
} REQUIRED_PAGES_INFO;

typedef struct
{
    UINT64 TableBasePhys;
    HOB_BALLOC *BumpAllocator;
    UINT64 AddrPhys;
    UINT64 AddrVirt;
    UINT64 Flags;
    UINT64 PageSize;
} MAP_REQUEST;

// functions decl.

static void PrintBootInfo(STAGING_BLOCK *block);
static EFI_STATUS GetFileSize(EFI_FILE_PROTOCOL *file, UINT64 *outSize);
static EFI_STATUS ReadKernelImage(IN const CHAR16 *path, OUT void **outImage, OUT UINT64 *outSize);
static UINT64 GetReclaimableMemorySize(MM_INITIAL_MAP *runtimeMap);
static void FillMmioInfo(STAGING_BLOCK *block);
static EFI_STATUS CreateKrnlMapping(EFI_PHYSICAL_ADDRESS pml4BasePhys,
                                    UINT64 pageTableBlockSize,
                                    STAGING_BLOCK *staging);
static EFI_STATUS MapBootPage(MAP_REQUEST *request);
static UINT64 HeaderPackedPages(UINT64 memoryMapSize);
static UINT64 PageTablePages(REQUIRED_PAGES_INFO *pagesInfo, UINT64 gopBufferSize);
static void CalcRequiredPages(ELF64_LOAD_INFO *loadInfo,
                              UINT64 mapSize,
                              UINT64 gopBufferSize,
                              REQUIRED_PAGES_INFO *result);
static STAGING_BLOCK *GetStagingBlock(REQUIRED_PAGES_INFO *pagesInfo, UINT64 totalReclaimable);
static BOOL LoadKernel(void *image, UINT64 imageSize, STAGING_BLOCK *staging);
static EFI_STATUS LoadMemoryMap(HO_PHYSICAL_ADDRESS mapBasePhys, UINT64 maxSize, OUT UINTN *memoryMap);
static void JumpToKernel(STAGING_BLOCK *block);

// functions

HO_KERNEL_API EFI_STATUS
StagingKernel(const CHAR16 *path)
{
    ConsoleWriteStr(L"Loading kernel from: ");
    ConsoleWriteStr(path);
    ConsoleWriteStr(L"\r\n");

    EFI_STATUS status = EFI_SUCCESS;
    void *image = NULL;
    UINT64 imageSize = 0;
    MM_INITIAL_MAP *memoryMap = NULL;
    STAGING_BLOCK *block = NULL;

    status = ReadKernelImage(path, &image, &imageSize);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to read kernel image: ", status);
        goto handle_error;
    }
    PRINT_SIZ_WITH_MESSAGE("Kernel image size: ", imageSize);

    memoryMap = GetLoaderRuntimeMemoryMap();
    if (memoryMap == NULL)
    {
        status = EFI_OUT_OF_RESOURCES;
        PRINT_HEX_WITH_MESSAGE("Failed to get memory map: ", status);
        goto handle_error;
    }

    ELF64_LOAD_INFO elfInfo;
    memset(&elfInfo, 0, sizeof(ELF64_LOAD_INFO));
    if (!Elf64GetLoadInfo(image, imageSize, &elfInfo))
    {
        ConsoleWriteStr(L"Invalid ELF64 kernel image\r\n");
        status = EFI_LOAD_ERROR;
        goto handle_error;
    }

    PrintFormatAddressRange(L"Kernel virtual address range", elfInfo.MinAddrVirt, elfInfo.VirtSpanPages << 12);
    if (elfInfo.MinAddrVirt != elfInfo.EntryVirt || elfInfo.MinAddrVirt != KRNL_ENTRY_VA)
    {
        ConsoleWriteStr(L"FATAL: Non-standard kernel base address or entry point\r\n");
        status = EFI_UNSUPPORTED;
        goto handle_error;
    }

    REQUIRED_PAGES_INFO pagesInfo;
    memset(&pagesInfo, 0, sizeof(REQUIRED_PAGES_INFO));
    ConsoleWriteStr(L"Calculating memory requirements...\r\n");
    CalcRequiredPages(&elfInfo, memoryMap->Size, g_GOP->Mode->FrameBufferSize, &pagesInfo);
    UINT64 totalPages = pagesInfo.TotalPages;
    PRINT_SIZ_WITH_MESSAGE("Total memory required for staging block: ", totalPages << 12);

    UINT64 totalReclaimable = GetReclaimableMemorySize(memoryMap);
    PRINT_SIZ_WITH_MESSAGE("Total reclaimable memory: ", totalReclaimable);

    block = GetStagingBlock(&pagesInfo, totalReclaimable);
    PRINT_HEX_WITH_MESSAGE("Staging block initialized at: ", (HO_PHYSICAL_ADDRESS)block);
    PrintBootInfo(block);

    if (LoadKernel(image, imageSize, block) == FALSE)
    {
        ConsoleWriteStr(L"Failed to load kernel image\r\n");
        status = EFI_LOAD_ERROR;
        goto handle_error;
    }

handle_error:
    if (memoryMap)
        (void)g_ST->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)memoryMap, memoryMap->DescriptorTotalSize >> 12);
    if (image)
        (void)g_ST->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)image, HO_ALIGN_UP(imageSize, PAGE_4KB) >> 12);
    if (status != EFI_SUCCESS)
        return status;

    PrintFormatAddressRange(L"Copying memory map to ", block->MemoryMapPhys, block->MemoryMapSize);
    status = CreateKrnlMapping(block->PageTablePhys, block->PageTableSize, block);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to create kernel page tables: ", status);
        return status;
    }

    PRINT_HEX_WITH_MESSAGE("Activating new page tables at ", block->PageTablePhys);
    ConsoleWriteStr(L"!!Attention!! UEFI Boot Services will be terminated!\r\n");
    UINTN memoryMapKey;
    status = LoadMemoryMap(block->MemoryMapPhys, block->MemoryMapSize, &memoryMapKey);
    if (status != EFI_SUCCESS)
    {
        return status;
    }
    status = g_ST->BootServices->ExitBootServices(gImageHandle, memoryMapKey);
    if (status != EFI_SUCCESS)
    {
        return status;
    }
    
    LoadCR3(block->PageTablePhys);
    (void)SetupGdt((void *)block->GdtPhys, block->GdtSize);
    JumpToKernel(block);

    // Should never reach here
    return EFI_SUCCESS;
}

// static functions

static void
PrintBootInfo(STAGING_BLOCK *block)
{
    ConsoleWriteStr(L"\r\nCopyright (c) 2025 HimuOS, starting HimuOS...\r\n");
    PRINT_HEX_WITH_MESSAGE("Staging Block Address: ", (HO_PHYSICAL_ADDRESS)block);

    PRINT_SIZ_WITH_MESSAGE("Total Reclaimable Memory: ", block->TotalReclaimableMem);
    PRINT_SIZ_WITH_MESSAGE("Staging Block Size: ", block->Size);
    PRINT_HEX_WITH_MESSAGE("Kernel Entry Point: ", block->KrnlEntryVirt);
    PRINT_ADDR_RANGE_WITH_MESSAGE("Staging Block", block->BasePhys, block->Size);
    PRINT_ADDR_RANGE_WITH_MESSAGE("- Header", block->BasePhys, block->InfoSize + block->MemoryMapSize + block->GdtSize);
    PRINT_ADDR_RANGE_WITH_MESSAGE("- Page Tables", block->PageTablePhys, block->PageTableSize);
    PRINT_ADDR_RANGE_WITH_MESSAGE("- Kernel Image", block->KrnlEntryPhys, block->KrnlDataSize + block->KrnlCodeSize);
    PRINT_ADDR_RANGE_WITH_MESSAGE("- Kernel Stack", block->KrnlStackPhys, block->KrnlStackSize);
    PRINT_ADDR_RANGE_WITH_MESSAGE("Frame Buffer", block->FramebufferPhys, block->FramebufferSize);
    ConsoleWriteStr(L"Resolution: ");
    ConsoleWriteUInt64(block->HorizontalResolution);
    ConsoleWriteStr(L"x");
    ConsoleWriteUInt64(block->VerticalResolution);
    ConsoleWriteStr(L"\r\n\r\n");
}

static EFI_STATUS
GetFileSize(EFI_FILE_PROTOCOL *file, UINT64 *outSize)
{
    struct EFI_GUID fileInfoGuid = EFI_FILE_INFO_GUID;
    UINTN bufferSize = 0;
    EFI_FILE_INFO *info = NULL;

    EFI_STATUS status = file->GetInfo(file, &fileInfoGuid, &bufferSize, NULL);
    if (GetStatusCodeLow(status) != EFI_BUFFER_TOO_SMALL)
    {
        return status;
    }

    status = g_ST->BootServices->AllocatePool(EfiBootServicesData, bufferSize, (void **)&info);
    if (EFI_ERROR(status))
    {
        return status;
    }

    status = file->GetInfo(file, &fileInfoGuid, &bufferSize, info);
    if (!EFI_ERROR(status))
    {
        *outSize = info->FileSize;
    }

    (void)g_ST->BootServices->FreePool(info);
    return status;
}

static EFI_STATUS
ReadKernelImage(IN const CHAR16 *path, OUT void **outImage, OUT UINT64 *outSize)
{
    EFI_STATUS status;
    EFI_FILE_PROTOCOL *rootDir = NULL;
    EFI_FILE_PROTOCOL *kernelFile = NULL;

    status = g_FSP->OpenVolume(g_FSP, &rootDir);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to open root volume: ", status);
        return status;
    }

    status = rootDir->Open(rootDir, &kernelFile, (CHAR16 *)path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to open kernel file: ", status);
        rootDir->Close(rootDir);
        return status;
    }

    UINT64 kernelFileSize = 0;
    status = GetFileSize(kernelFile, &kernelFileSize);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to get kernel file size: ", status);
        kernelFile->Close(kernelFile);
        rootDir->Close(rootDir);
        return status;
    }

    EFI_PHYSICAL_ADDRESS bufferPhys = 0;
    status = g_ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                               HO_ALIGN_UP(kernelFileSize, PAGE_4KB) >> 12, &bufferPhys);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to allocate memory for kernel file: ", status);
        kernelFile->Close(kernelFile);
        rootDir->Close(rootDir);
        return status;
    }

    UINTN readSize = kernelFileSize;
    status = kernelFile->Read(kernelFile, &readSize, (void *)bufferPhys);

    if (EFI_ERROR(status) || readSize != kernelFileSize)
    {
        PRINT_HEX_WITH_MESSAGE("Failed to read kernel file: ", status);
        kernelFile->Close(kernelFile);
        rootDir->Close(rootDir);
        (void)g_ST->BootServices->FreePages(bufferPhys, HO_ALIGN_UP(kernelFileSize, PAGE_4KB) >> 12);
        return EFI_DEVICE_ERROR;
    }

    UINT32 crc32 = 0;
    status = g_ST->BootServices->CalculateCrc32((void *)bufferPhys, kernelFileSize, &crc32);
    ConsoleWriteStr(L"Kernel image CRC32: ");
    ConsoleWriteHex(crc32);
    ConsoleWriteStr(L"\r\n");

    kernelFile->Close(kernelFile);
    rootDir->Close(rootDir);

    *outImage = (void *)bufferPhys;
    *outSize = kernelFileSize;
    return EFI_SUCCESS;
}

static UINT64
GetReclaimableMemorySize(MM_INITIAL_MAP *runtimeMap)
{
    UINTN mapEntries = runtimeMap->DescriptorTotalSize / runtimeMap->DescriptorSize;
    UINTN totalReclaimable = 0;

    EFI_MEMORY_DESCRIPTOR *desc = runtimeMap->Segs;

    for (UINT64 i = 0; i < mapEntries; i++)
    {
        if (IsUsableMemory(desc->Type))
        {
            totalReclaimable += desc->NumberOfPages << 12;
        }
        desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)desc + runtimeMap->DescriptorSize);
    }
    return totalReclaimable;
}

static void
FillMmioInfo(STAGING_BLOCK *block)
{
    block->VideoModeType = VIDEO_MODE_TYPE_UEFI;
    block->PixelFormat =
        (g_GOP->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor) ? PIXEL_FORMAT_BGR : PIXEL_FORMAT_RGB;
    block->FramebufferPhys = g_GOP->Mode->FrameBufferBase;
    block->FramebufferVirt = MMIO_BASE_VA;
    block->FramebufferSize = g_GOP->Mode->FrameBufferSize;
    block->HorizontalResolution = g_GOP->Mode->Info->HorizontalResolution;
    block->VerticalResolution = g_GOP->Mode->Info->VerticalResolution;
    block->PixelsPerScanLine = g_GOP->Mode->Info->PixelsPerScanLine;
}

static EFI_STATUS
CreateKrnlMapping(EFI_PHYSICAL_ADDRESS pml4BasePhys, UINT64 pageTableBlockSize, STAGING_BLOCK *staging)
{
    /* NOTE & TODO: During the boot staging phase, all pages (except kernel code pages) are mapped as uncached.
       This facilitates debugging and prevents extremely erratic behavior on some machines.
       In the staging phase, HimuOS will remap the page tables during the mm initialization stage. */

    HOB_BALLOC alloc;
    memset(&alloc, 0, sizeof(HOB_BALLOC));

    EFI_STATUS status = HobAllocCreate(pml4BasePhys, &alloc, pageTableBlockSize >> 12);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to create page table allocator: ", status);
        return status;
    }

    UINT64 pml4Phys = (UINT64)HobAlloc(&alloc, PAGE_4KB, PAGE_4KB);
    if (pml4Phys == 0)
    {
        PRINT_HEX_WITH_MESSAGE("Failed to allocate PML4: ", EFI_OUT_OF_RESOURCES);
        return EFI_OUT_OF_RESOURCES;
    }

    MAP_REQUEST request;
    memset(&request, 0, sizeof(MAP_REQUEST));
    request.TableBasePhys = pml4Phys;
    request.BumpAllocator = &alloc;

    // Identity map lower 4GB
    request.PageSize = PAGE_2MB;
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_PAGE_SIZE;
    for (UINT64 currentPhysAddr = 0; currentPhysAddr < 0x100000000; currentPhysAddr += PAGE_2MB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = currentPhysAddr;
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            PRINT_HEX_WITH_MESSAGE("Failed to map lower 4GB at ", currentPhysAddr);
            return status;
        }
    }

    // Map boot info (Staging Block, Memory Map, etc.)
    request.PageSize = PAGE_4KB;
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE;
    UINT64 startPhysAddr = staging->BasePhys;
    UINT64 endPhysAddr = staging->KrnlEntryPhys;
    PRINT_ADDR_MAP("Mapping boot info: ", startPhysAddr, KRNL_BASE_VA, endPhysAddr - startPhysAddr);
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_BASE_VA + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            PRINT_HEX_WITH_MESSAGE("Failed to map boot info at ", currentPhysAddr);
            return status;
        }
    }

    // Map kernel code
    request.Flags = 0; // Executable
    startPhysAddr = staging->KrnlEntryPhys;
    endPhysAddr = startPhysAddr + staging->KrnlCodeSize;
    PRINT_ADDR_MAP("Mapping kernel code segment: ", startPhysAddr, KRNL_ENTRY_VA, endPhysAddr - startPhysAddr);
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_ENTRY_VA + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            PRINT_HEX_WITH_MESSAGE("Failed to map kernel image at ", currentPhysAddr);
            return status;
        }
    }

    // Map kernel data
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE;
    startPhysAddr = staging->KrnlEntryPhys + staging->KrnlCodeSize;
    endPhysAddr = startPhysAddr + staging->KrnlDataSize;
    PRINT_ADDR_MAP("Mapping kernel data segment: ", startPhysAddr, KRNL_ENTRY_VA + staging->KrnlCodeSize,
                   endPhysAddr - startPhysAddr);
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_ENTRY_VA + staging->KrnlCodeSize + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            PRINT_HEX_WITH_MESSAGE("Failed to map kernel image at ", currentPhysAddr);
            return status;
        }
    }

    // Map kernel stack
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE;
    startPhysAddr = staging->KrnlStackPhys;
    endPhysAddr = startPhysAddr + staging->KrnlStackSize;
    PRINT_ADDR_MAP("Mapping kernel stack: ", startPhysAddr, KRNL_STACK_VA, endPhysAddr - startPhysAddr);
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_STACK_VA + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            PRINT_HEX_WITH_MESSAGE("Failed to map kernel stack at ", currentPhysAddr);
            return status;
        }
    }

    // Map MMIO (GOP framebuffer)
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_NO_EXECUTE;
    startPhysAddr = staging->FramebufferPhys;
    endPhysAddr = startPhysAddr + staging->FramebufferSize;
    PRINT_ADDR_MAP("Mapping MMIO (GOP framebuffer): ", startPhysAddr, staging->FramebufferVirt,
                   endPhysAddr - startPhysAddr);
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = staging->FramebufferVirt + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            PRINT_HEX_WITH_MESSAGE("Failed to map MMIO at ", currentPhysAddr);
            return status;
        }
    }

    return EFI_SUCCESS;
}

static EFI_STATUS
MapBootPage(MAP_REQUEST *request)
{
    if (request->PageSize != PAGE_4KB && request->PageSize != PAGE_2MB && request->PageSize != PAGE_1GB)
    {
        PRINT_HEX_WITH_MESSAGE("Unsupported page size: ", request->PageSize);
        return EFI_INVALID_PARAMETER;
    }

    UINT64 alignedAddrPhys = HO_ALIGN_DOWN(request->AddrPhys, request->PageSize);
    UINT64 alignedAddrVirt = HO_ALIGN_DOWN(request->AddrVirt, request->PageSize);
    HOB_BALLOC *allocator = request->BumpAllocator;
    PAGE_TABLE_ENTRY *pml4 = (PAGE_TABLE_ENTRY *)request->TableBasePhys;
    UINT64 flags = request->Flags;

    UINT64 pml4Index = PML4_INDEX(alignedAddrVirt);
    if (!(pml4[pml4Index] & PTE_PRESENT))
    {
        UINT64 pdptPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (pdptPhys == 0)
        {
            PRINT_HEX_WITH_MESSAGE("Failed to allocate PDPT: ", EFI_OUT_OF_RESOURCES);
            return EFI_OUT_OF_RESOURCES;
        }
        pml4[pml4Index] = pdptPhys | PTE_PRESENT | PTE_WRITABLE;
    }

    PAGE_TABLE_ENTRY *pdpt = (PAGE_TABLE_ENTRY *)(pml4[pml4Index] & PAGE_MASK);
    UINT64 pdptIndex = PDPT_INDEX(alignedAddrVirt);
    if (request->PageSize == PAGE_1GB)
    {
        if (pdpt[pdptIndex] & PTE_PRESENT)
        {
            PRINT_HEX_WITH_MESSAGE("Virtual address already mapped: ", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
        pdpt[pdptIndex] = alignedAddrPhys | flags | PTE_PRESENT;
        return EFI_SUCCESS;
    }

    if (!(pdpt[pdptIndex] & PTE_PRESENT))
    {
        UINT64 pdPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (pdPhys == 0)
        {
            PRINT_HEX_WITH_MESSAGE("Failed to allocate PD: ", EFI_OUT_OF_RESOURCES);
            return EFI_OUT_OF_RESOURCES;
        }
        pdpt[pdptIndex] = pdPhys | PTE_PRESENT | PTE_WRITABLE;
    }

    PAGE_TABLE_ENTRY *pd = (PAGE_TABLE_ENTRY *)(pdpt[pdptIndex] & PAGE_MASK);
    UINT64 pdIndex = PD_INDEX(alignedAddrVirt);
    if (request->PageSize == PAGE_2MB)
    {
        if (pd[pdIndex] & PTE_PRESENT)
        {
            PRINT_HEX_WITH_MESSAGE("Virtual address already mapped: ", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
        pd[pdIndex] = alignedAddrPhys | flags | PTE_PRESENT;
        return EFI_SUCCESS;
    }

    if (!(pd[pdIndex] & PTE_PRESENT))
    {
        UINT64 ptPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (ptPhys == 0)
        {
            PRINT_HEX_WITH_MESSAGE("Failed to allocate PT: ", EFI_OUT_OF_RESOURCES);
            PRINT_HEX_WITH_MESSAGE("Virtual address to map: ", alignedAddrVirt);
            PRINT_HEX_WITH_MESSAGE("Allocator state: ", allocator->Base + allocator->Offset);
            return EFI_OUT_OF_RESOURCES;
        }
        pd[pdIndex] = ptPhys | PTE_PRESENT | PTE_WRITABLE;
    }

    PAGE_TABLE_ENTRY *pt = (PAGE_TABLE_ENTRY *)(pd[pdIndex] & PAGE_MASK);
    UINT64 ptIndex = PT_INDEX(alignedAddrVirt);
    if (pt[ptIndex] & PTE_PRESENT)
    {
        PRINT_HEX_WITH_MESSAGE("Virtual address already mapped: ", alignedAddrVirt);
        return EFI_INVALID_PARAMETER;
    }

    pt[ptIndex] = alignedAddrPhys | flags | PTE_PRESENT;
    return EFI_SUCCESS;
}

static UINT64
HeaderPackedPages(UINT64 memoryMapSize)
{
    UINT64 pages = HO_ALIGN_UP(memoryMapSize + sizeof(STAGING_BLOCK) + sizeof(GLOBAL_DESCRIPTOR_TABLE), PAGE_4KB) >> 12;
    return pages + 2; // Anyway, reserve 2 extra pages for safety
}

static UINT64
PageTablePages(REQUIRED_PAGES_INFO *pagesInfo, UINT64 gopBufferSize)
{
    UINT64 pml4Pages = 1;
    UINT64 pdptPages = 1   // for Lower 4GB Identity Map (PML4[0])
                       + 1 // for Higher-Half Kernel area (PML4[256] for 0xFFFF80...)
                       + 1 // for MMIO area (PML4[511] for 0xFFFFC0...)
        ;
    UINT64 pdPages = 4   // for Lower 4GB Identity Map (each PDPT entry maps 1GB, so 4 PDPT entries needed)
                     + 1 // Boot info (staging block, memory map, page tables)
                     + 1 // Kernel code & data segments
                     + 1 // Kernel stack
                     + 1 // MMIO area, GOP
        ;

    UINT64 ptPages = 0;
    ptPages += CalcPagesToStoreEntries(pagesInfo->HeaderPages, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);
    UINT64 kernelPhysPages = pagesInfo->KernelCodePages + pagesInfo->KernelDataPages;
    ptPages += CalcPagesToStoreEntries(kernelPhysPages, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);
    ptPages += CalcPagesToStoreEntries(KRNL_STACK_PAGES, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);
    ptPages += CalcPagesToStoreEntries(gopBufferSize >> 12, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);

    UINT64 totalPages = pml4Pages + pdptPages + pdPages + ptPages;
    UINT64 itselfPages = CalcPagesToStoreEntries(totalPages, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);

    return totalPages + itselfPages + 1; // 1 for safety margin
}

static void
CalcRequiredPages(ELF64_LOAD_INFO *loadInfo, uint64_t mapSize, uint64_t gopBufferSize, REQUIRED_PAGES_INFO *result)
{
    result->KernelCodePages = loadInfo->ExecPhysPages;
    result->KernelDataPages = loadInfo->DataPhysPages;
    result->KernelSpanPages = loadInfo->VirtSpanPages;
    result->HeaderPages = HeaderPackedPages(mapSize);
    result->PageTablePages = PageTablePages(result, gopBufferSize);

    result->TotalPages = result->KernelSpanPages + result->HeaderPages + result->PageTablePages + KRNL_STACK_PAGES;
}

static STAGING_BLOCK *
GetStagingBlock(REQUIRED_PAGES_INFO *pagesInfo, UINT64 totalReclaimable)
{
    UINT64 basePhys = 0;

    EFI_STATUS status =
        g_ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, pagesInfo->TotalPages, &basePhys);
    if (EFI_ERROR(status))
    {
        PRINT_HEX_WITH_MESSAGE("Failed to allocate pages for staging block: ", status);
        return NULL;
    }

    STAGING_BLOCK *block = (STAGING_BLOCK *)basePhys;
    memset(block, 0, sizeof(STAGING_BLOCK));

    block->Magic = STAGING_BLOCK_MAGIC;
    block->BasePhys = basePhys;
    block->BaseVirt = KRNL_BASE_VA;
    block->Size = pagesInfo->TotalPages << 12;
    block->TotalReclaimableMem = totalReclaimable;
    FillMmioInfo(block);

    block->InfoSize = sizeof(STAGING_BLOCK);
    block->GdtSize = sizeof(GLOBAL_DESCRIPTOR_TABLE);
    block->MemoryMapSize = (pagesInfo->HeaderPages << 12) - sizeof(STAGING_BLOCK) - sizeof(GLOBAL_DESCRIPTOR_TABLE);
    block->PageTableSize = pagesInfo->PageTablePages << 12;
    block->KrnlCodeSize = pagesInfo->KernelCodePages << 12;
    block->KrnlDataSize = pagesInfo->KernelDataPages << 12;
    block->KrnlVirtSize = pagesInfo->KernelSpanPages << 12;
    block->KrnlStackSize = KRNL_STACK_SIZE;

    block->GdtPhys = basePhys + block->InfoSize;
    block->MemoryMapPhys = block->GdtPhys + block->GdtSize;
    block->PageTablePhys = block->MemoryMapPhys + block->MemoryMapSize;
    block->KrnlEntryPhys = block->PageTablePhys + block->PageTableSize;
    block->KrnlStackPhys = block->KrnlEntryPhys + block->KrnlDataSize + block->KrnlCodeSize;

    block->GdtVirt = block->BaseVirt + block->InfoSize;
    block->MemoryMapVirt = block->GdtVirt + block->GdtSize;
    block->PageTableVirt = block->MemoryMapVirt + block->MemoryMapSize;
    block->KrnlEntryVirt = KRNL_ENTRY_VA;
    block->KrnlStackVirt = KRNL_STACK_VA;

    return block;
}

static BOOL
LoadKernel(void *image, UINT64 imageSize, STAGING_BLOCK *staging)
{
    ELF64_LOAD_BUFFER_PARAMS params;
    memset(&params, 0, sizeof(ELF64_LOAD_BUFFER_PARAMS));

    params.BaseVirt = staging->KrnlEntryVirt;
    params.Image = image;
    params.ImageSize = imageSize;
    params.Base = (void *)staging->KrnlEntryPhys;
    params.BaseSize = staging->KrnlCodeSize + staging->KrnlDataSize;

    PrintFormatAddressRange(L"Loading kernel to", (UINT64)params.Base, params.BaseSize);
    return Elf64LoadToBuffer(&params);
}

static EFI_STATUS
LoadMemoryMap(HO_VIRTUAL_ADDRESS mapBasePhys, UINT64 maxSize, OUT UINTN *memoryMapKey)
{
    MM_INITIAL_MAP *map = InitMemoryMap((void *)mapBasePhys, maxSize);
    EFI_STATUS status = FillMemoryMap(map);
    if (status != EFI_SUCCESS)
        return status;
    *memoryMapKey = map->MemoryMapKey;
    return EFI_SUCCESS;
}

static void
JumpToKernel(STAGING_BLOCK *block)
{
    HO_VIRTUAL_ADDRESS stackTopVirt = block->KrnlStackVirt + block->KrnlStackSize;
    HO_VIRTUAL_ADDRESS blockVirt = block->BaseVirt;
    HO_VIRTUAL_ADDRESS entryVirt = block->KrnlEntryVirt;

    __asm__ __volatile__("mov %[rsp], %%rsp\n\t" // Set stack pointer
                         "mov %[arg], %%rdi\n\t" // First argument in rdi
                         :
                         : [rsp] "r"(stackTopVirt), [arg] "r"(blockVirt));

    __asm__ __volatile__("jmp *%[entry]\n\t" // Jump to kernel entry point
                         :
                         : [entry] "r"(entryVirt));
}