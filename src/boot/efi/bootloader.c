#include "bootloader.h"
#include "alloc.h"
#include "io.h"
#include "common/elf/elf.h"
#include "arch/amd64/pm.h"

// typedefs
typedef struct
{
    HO_PHYSICAL_ADDRESS StagingBlockPhys;
    UINT64 TotalReclaimableMem;
} SCAN_MEMMAP_RESULT;

typedef struct
{
    UINT64 HeaderPages;
    UINT64 KernelCodePages;
    UINT64 KernelDataPages;
    UINT64 KernelSpanPages;
    UINT64 PageTablePages;
    UINT64 TotalPages;
} REQUIRED_PAGES_INFO;

typedef struct
{
    BOOL NotPresent;
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
static EFI_STATUS CreateKrnlMapping(EFI_PHYSICAL_ADDRESS pml4BasePhys, UINT64 pageTableBlockSize, STAGING_BLOCK *staging);
static EFI_STATUS MapBootPage(MAP_REQUEST *request);
static UINT64 HeaderPackedPages(UINT64 memoryMapSize);
static UINT64 PageTablePages(REQUIRED_PAGES_INFO *pagesInfo, UINT64 gopBufferSize);
static UINT64 KrnlStackPages();
static void CalcRequiredPages(ELF64_LOAD_INFO *loadInfo, UINT64 mapSize, UINT64 gopBufferSize, REQUIRED_PAGES_INFO *result);
static STAGING_BLOCK *GetStagingBlock(REQUIRED_PAGES_INFO *pagesInfo, UINT64 totalReclaimable);
static BOOL LoadKernel(void *image, UINT64 imageSize, STAGING_BLOCK *staging);
static EFI_STATUS LoadMemoryMap(HO_PHYSICAL_ADDRESS mapBasePhys, UINT64 maxSize, OUT UINTN *memoryMap);
static void JumpToKernel(STAGING_BLOCK *block);

// functions

HO_KERNEL_API EFI_STATUS
StagingKernel(const CHAR16 *path)
{
    ConsoleFormatWrite(L"Loading kernel from: %s\r\n", path);

    EFI_STATUS status = EFI_SUCCESS;
    void *image = NULL;
    UINT64 imageSize = 0;
    MM_INITIAL_MAP *memoryMap = NULL;
    STAGING_BLOCK *block = NULL;

    status = ReadKernelImage(path, &image, &imageSize);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to read kernel image: %k (0x%x)\r\n", status, status);
        goto handle_error;
    }
    ConsoleFormatWrite(L"Kernel image size: %u byte\r\n", imageSize);

    memoryMap = GetLoaderRuntimeMemoryMap();
    if (memoryMap == NULL)
    {
        status = EFI_OUT_OF_RESOURCES;
        ConsoleFormatWrite(L"Failed to get memory map: %k (0x%x)\r\n", status, status);
        goto handle_error;
    }

    ELF64_LOAD_INFO elfInfo;
    memset(&elfInfo, 0, sizeof(ELF64_LOAD_INFO));
    if (!Elf64GetLoadInfo(image, imageSize, &elfInfo))
    {
        ConsoleFormatWrite(L"Invalid ELF64 kernel image\r\n");
        status = EFI_LOAD_ERROR;
        goto handle_error;
    }

    ConsoleFormatWrite(L"Kernel virtual address range: 0x%x -> 0x%x (%u byte)\r\n", 
                       elfInfo.MinAddrVirt, 
                       elfInfo.MinAddrVirt + (elfInfo.VirtSpanPages << 12),
                       elfInfo.VirtSpanPages << 12);

    if (elfInfo.MinAddrVirt != elfInfo.EntryVirt || elfInfo.MinAddrVirt != KRNL_ENTRY_VA)
    {
        ConsoleFormatWrite(L"FATAL: Non-standard kernel base address or entry point\r\n");
        status = EFI_UNSUPPORTED;
        goto handle_error;
    }

    REQUIRED_PAGES_INFO pagesInfo;
    memset(&pagesInfo, 0, sizeof(REQUIRED_PAGES_INFO));
    ConsoleFormatWrite(L"Calculating memory requirements...\r\n");
    
    CalcRequiredPages(&elfInfo, memoryMap->Size, g_GOP->Mode->FrameBufferSize, &pagesInfo);
    UINT64 totalPages = pagesInfo.TotalPages;
    
    ConsoleFormatWrite(L"Total memory required for staging block: %u byte\r\n", totalPages << 12);

    UINT64 totalReclaimable = GetReclaimableMemorySize(memoryMap);
    ConsoleFormatWrite(L"Total reclaimable memory: %u byte\r\n", totalReclaimable);

    block = GetStagingBlock(&pagesInfo, totalReclaimable);
    ConsoleFormatWrite(L"Staging block initialized at: %p\r\n", block);
    
    PrintBootInfo(block);

    if (LoadKernel(image, imageSize, block) == FALSE)
    {
        ConsoleFormatWrite(L"Failed to load kernel image\r\n");
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

    ConsoleFormatWrite(L"Copying memory map to 0x%x (Size: %u byte)\r\n", block->MemoryMapPhys, block->MemoryMapSize);
    
    status = CreateKrnlMapping(block->PageTablePhys, block->PageTableSize, block);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to create kernel page tables: %k (0x%x)\r\n", status, status);
        return status;
    }

    ConsoleFormatWrite(L"Activating new page tables at 0x%x\r\n", block->PageTablePhys);
    ConsoleFormatWrite(L"!!Attention!! UEFI Boot Services will be terminated!\r\n");
    
    UINTN memoryMapKey;
    status = LoadMemoryMap(block->MemoryMapPhys, block->MemoryMapSize, &memoryMapKey);
    if (status != EFI_SUCCESS)
    {
        ConsoleFormatWrite(L"Failed to load memory map: %k (0x%x)\r\n", status, status);
        return status;
    }
    
    CPU_CORE_LOCAL_DATA *core = InitCpuCoreLocalData((void *)block->CoreLocalDataPhys, block->CoreLocalDataSize);
    if (core == NULL)
    {
        ConsoleFormatWrite(L"Failed to initialize CPU core local data\r\n");
        return EFI_OUT_OF_RESOURCES;
    }
    
    status = g_ST->BootServices->ExitBootServices(gImageHandle, memoryMapKey);
    if (status != EFI_SUCCESS)
    {
        return status;
    }

    LoadCR3(block->PageTablePhys);
    LoadGdtAndTss(core);
    JumpToKernel(block);

    return EFI_SUCCESS;
}

// static functions

static void
PrintBootInfo(STAGING_BLOCK *block)
{
    ConsoleFormatWrite(L"\r\nCopyright (c) 2025 HimuOS, starting HimuOS...\r\n");
    ConsoleFormatWrite(L"Staging Block Address: %p\r\n", block);
    ConsoleFormatWrite(L"Total Reclaimable Memory: %u byte\r\n", block->TotalReclaimableMem);
    ConsoleFormatWrite(L"Staging Block Size: %u byte\r\n", block->Size);
    ConsoleFormatWrite(L"Kernel Entry Point: 0x%x\r\n", block->KrnlEntryVirt);

    #define LOG_MAP(name, phys, size) \
        ConsoleFormatWrite(L"%s 0x%x -> 0x%x (%u byte)\r\n", name, (UINT64)phys, (UINT64)phys + size, size)

    LOG_MAP(L"Staging Block:    ", block->BasePhys, block->Size);
    LOG_MAP(L"- Header:         ", block->BasePhys, block->InfoSize + block->MemoryMapSize + block->CoreLocalDataSize);
    LOG_MAP(L"- Page Tables:    ", block->PageTablePhys, block->PageTableSize);
    LOG_MAP(L"- Kernel Image:   ", block->KrnlEntryPhys, block->KrnlDataSize + block->KrnlCodeSize);
    LOG_MAP(L"- Kernel Stack:   ", block->KrnlStackPhys, block->KrnlStackSize);
    LOG_MAP(L"Frame Buffer:     ", block->FramebufferPhys, block->FramebufferSize);

    #undef LOG_MAP

    ConsoleFormatWrite(L"Resolution: %ux%u\r\n\r\n", block->HorizontalResolution, block->VerticalResolution);
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
        ConsoleFormatWrite(L"Failed to open root volume: %k (0x%x)\r\n", status, status);
        return status;
    }

    status = rootDir->Open(rootDir, &kernelFile, (CHAR16 *)path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to open kernel file: %k (0x%x)\r\n", status, status);
        rootDir->Close(rootDir);
        return status;
    }

    UINT64 kernelFileSize = 0;
    status = GetFileSize(kernelFile, &kernelFileSize);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to get kernel file size: %k (0x%x)\r\n", status, status);
        kernelFile->Close(kernelFile);
        rootDir->Close(rootDir);
        return status;
    }

    EFI_PHYSICAL_ADDRESS bufferPhys = 0;
    status = g_ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData,
                                               HO_ALIGN_UP(kernelFileSize, PAGE_4KB) >> 12, &bufferPhys);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to allocate memory for kernel file: %k (0x%x)\r\n", status, status);
        kernelFile->Close(kernelFile);
        rootDir->Close(rootDir);
        return status;
    }

    UINTN readSize = kernelFileSize;
    status = kernelFile->Read(kernelFile, &readSize, (void *)bufferPhys);

    if (EFI_ERROR(status) || readSize != kernelFileSize)
    {
        ConsoleFormatWrite(L"Failed to read kernel file: %k (0x%x)\r\n", status, status);
        kernelFile->Close(kernelFile);
        rootDir->Close(rootDir);
        (void)g_ST->BootServices->FreePages(bufferPhys, HO_ALIGN_UP(kernelFileSize, PAGE_4KB) >> 12);
        return EFI_DEVICE_ERROR;
    }

    UINT32 crc32 = 0;
    status = g_ST->BootServices->CalculateCrc32((void *)bufferPhys, kernelFileSize, &crc32);
    ConsoleFormatWrite(L"Kernel image CRC32: 0x%x\r\n", crc32);

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
    HOB_BALLOC alloc;
    memset(&alloc, 0, sizeof(HOB_BALLOC));

    EFI_STATUS status = HobAllocCreate(pml4BasePhys, &alloc, pageTableBlockSize >> 12);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to create page table allocator: %k (0x%x)\r\n", status, status);
        return status;
    }

    UINT64 pml4Phys = (UINT64)HobAlloc(&alloc, PAGE_4KB, PAGE_4KB);
    if (pml4Phys == 0)
    {
        ConsoleFormatWrite(L"Failed to allocate PML4: %k\r\n", EFI_OUT_OF_RESOURCES);
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
            ConsoleFormatWrite(L"Failed to map lower 4GB at 0x%x\r\n", currentPhysAddr);
            return status;
        }
    }

    // Helper macro for consistent mapping log
    #define LOG_MAPPING(msg, start, virt, size) \
        ConsoleFormatWrite(L"%s 0x%x -> 0x%x (%u byte)\r\n", msg, start, virt, size)

    // Map boot info
    request.PageSize = PAGE_4KB;
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE;
    UINT64 startPhysAddr = staging->BasePhys;
    UINT64 endPhysAddr = staging->KrnlEntryPhys;
    LOG_MAPPING(L"Mapping BOOT_INFO:", startPhysAddr, KRNL_BASE_VA, endPhysAddr - startPhysAddr);
    
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_BASE_VA + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            ConsoleFormatWrite(L"Failed to map boot info at 0x%x\r\n", currentPhysAddr);
            return status;
        }
    }

    // Map kernel code
    request.Flags = 0; // Executable
    startPhysAddr = staging->KrnlEntryPhys;
    endPhysAddr = startPhysAddr + staging->KrnlCodeSize;
    LOG_MAPPING(L"Mapping KRNL_CODE:", startPhysAddr, KRNL_ENTRY_VA, endPhysAddr - startPhysAddr);
    
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_ENTRY_VA + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            ConsoleFormatWrite(L"Failed to map kernel image at 0x%x\r\n", currentPhysAddr);
            return status;
        }
    }

    // Map kernel data
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE;
    startPhysAddr = staging->KrnlEntryPhys + staging->KrnlCodeSize;
    endPhysAddr = startPhysAddr + staging->KrnlDataSize;
    LOG_MAPPING(L"Mapping KRNL_DATA:", startPhysAddr, KRNL_ENTRY_VA + staging->KrnlCodeSize, endPhysAddr - startPhysAddr);
    
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_ENTRY_VA + staging->KrnlCodeSize + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            ConsoleFormatWrite(L"Failed to map kernel data at 0x%x\r\n", currentPhysAddr);
            return status;
        }
    }

    // Map kernel stack
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE;
    startPhysAddr = staging->KrnlStackPhys;
    endPhysAddr = startPhysAddr + staging->KrnlStackSize;
    LOG_MAPPING(L"Mapping KRNL_STAK:", startPhysAddr, KRNL_STACK_VA, endPhysAddr - startPhysAddr);

    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_STACK_VA + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            ConsoleFormatWrite(L"Failed to map kernel stack at 0x%x\r\n", currentPhysAddr);
            return status;
        }
    }

    // Guard page
    request.NotPresent = TRUE;
    startPhysAddr = (UINT64)0;
    LOG_MAPPING(L"Mapping IST1_GDPG:", startPhysAddr, KRNL_STACK_VA + staging->KrnlStackSize, PAGE_4KB);
    
    request.AddrPhys = startPhysAddr;
    request.AddrVirt = KRNL_STACK_VA + staging->KrnlStackSize;
    status = MapBootPage(&request);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to map IST1 stack guard page\r\n");
        return status;
    }

    // Map IST1 stack
    request.NotPresent = FALSE;
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE;
    startPhysAddr = staging->KrnlIST1StackPhys;
    endPhysAddr = startPhysAddr + staging->KrnlStackSize;
    LOG_MAPPING(L"Mapping IST1_STAK:", startPhysAddr, KRNL_IST1_STACK_VA, endPhysAddr - startPhysAddr);
    
    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = KRNL_IST1_STACK_VA + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            ConsoleFormatWrite(L"Failed to map IST1 stack at 0x%x\r\n", currentPhysAddr);
            return status;
        }
    }

    // Map MMIO
    request.Flags = PTE_WRITABLE | PTE_CACHE_DISABLE | PTE_NO_EXECUTE;
    startPhysAddr = staging->FramebufferPhys;
    endPhysAddr = startPhysAddr + staging->FramebufferSize;
    LOG_MAPPING(L"Mapping MMIO_GOPB:", startPhysAddr, staging->FramebufferVirt, endPhysAddr - startPhysAddr);

    for (UINT64 currentPhysAddr = startPhysAddr; currentPhysAddr < endPhysAddr; currentPhysAddr += PAGE_4KB)
    {
        request.AddrPhys = currentPhysAddr;
        request.AddrVirt = staging->FramebufferVirt + (currentPhysAddr - startPhysAddr);
        status = MapBootPage(&request);
        if (EFI_ERROR(status))
        {
            ConsoleFormatWrite(L"Failed to map MMIO at 0x%x\r\n", currentPhysAddr);
            return status;
        }
    }

    #undef LOG_MAPPING
    return EFI_SUCCESS;
}

static EFI_STATUS
MapBootPage(MAP_REQUEST *request)
{
    if (request->PageSize != PAGE_4KB && request->PageSize != PAGE_2MB && request->PageSize != PAGE_1GB)
    {
        ConsoleFormatWrite(L"Unsupported page size: %u\r\n", request->PageSize);
        return EFI_INVALID_PARAMETER;
    }

    UINT64 alignedAddrPhys = HO_ALIGN_DOWN(request->AddrPhys, request->PageSize);
    UINT64 alignedAddrVirt = HO_ALIGN_DOWN(request->AddrVirt, request->PageSize);
    HOB_BALLOC *allocator = request->BumpAllocator;
    PAGE_TABLE_ENTRY *pml4 = (PAGE_TABLE_ENTRY *)request->TableBasePhys;
    UINT64 flags = request->Flags;
    UINT64 isPresent = (HO_LIKELY(!request->NotPresent)) ? PTE_PRESENT : 0;

    UINT64 pml4Index = PML4_INDEX(alignedAddrVirt);
    if (!(pml4[pml4Index] & PTE_PRESENT))
    {
        UINT64 pdptPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (pdptPhys == 0)
        {
            ConsoleFormatWrite(L"Failed to allocate PDPT: %k\r\n", EFI_OUT_OF_RESOURCES);
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
            ConsoleFormatWrite(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
        pdpt[pdptIndex] = alignedAddrPhys | flags | isPresent;
        return EFI_SUCCESS;
    }

    if (!(pdpt[pdptIndex] & PTE_PRESENT))
    {
        UINT64 pdPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (pdPhys == 0)
        {
            ConsoleFormatWrite(L"Failed to allocate PD: %k\r\n", EFI_OUT_OF_RESOURCES);
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
            ConsoleFormatWrite(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
            return EFI_INVALID_PARAMETER;
        }
        pd[pdIndex] = alignedAddrPhys | flags | isPresent;
        return EFI_SUCCESS;
    }

    if (!(pd[pdIndex] & PTE_PRESENT))
    {
        UINT64 ptPhys = (UINT64)HobAlloc(allocator, PAGE_4KB, PAGE_4KB);
        if (ptPhys == 0)
        {
            ConsoleFormatWrite(L"Failed to allocate PT: %k\r\n", EFI_OUT_OF_RESOURCES);
            ConsoleFormatWrite(L"Virtual address to map: 0x%x\r\n", alignedAddrVirt);
            ConsoleFormatWrite(L"Allocator state: 0x%x\r\n", allocator->Base + allocator->Offset);
            return EFI_OUT_OF_RESOURCES;
        }
        pd[pdIndex] = ptPhys | PTE_PRESENT | PTE_WRITABLE;
    }

    PAGE_TABLE_ENTRY *pt = (PAGE_TABLE_ENTRY *)(pd[pdIndex] & PAGE_MASK);
    UINT64 ptIndex = PT_INDEX(alignedAddrVirt);
    if (pt[ptIndex] & PTE_PRESENT)
    {
        ConsoleFormatWrite(L"Virtual address already mapped: 0x%x\r\n", alignedAddrVirt);
        return EFI_INVALID_PARAMETER;
    }

    pt[ptIndex] = alignedAddrPhys | flags | isPresent;
    return EFI_SUCCESS;
}

static UINT64
HeaderPackedPages(UINT64 memoryMapSize)
{
    UINT64 pages = HO_ALIGN_UP(memoryMapSize + sizeof(STAGING_BLOCK) + sizeof(CPU_CORE_LOCAL_DATA), PAGE_4KB) >> 12;
    return pages + 2; 
}

static UINT64
PageTablePages(REQUIRED_PAGES_INFO *pagesInfo, UINT64 gopBufferSize)
{
    UINT64 pml4Pages = 1;
    UINT64 pdptPages = 1 + 1 + 1;
    UINT64 pdPages = 4 + 1 + 1 + 1 + 1;

    UINT64 ptPages = 0;
    ptPages += CalcPagesToStoreEntries(pagesInfo->HeaderPages, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);
    UINT64 kernelPhysPages = pagesInfo->KernelCodePages + pagesInfo->KernelDataPages;
    ptPages += CalcPagesToStoreEntries(kernelPhysPages, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);
    ptPages += CalcPagesToStoreEntries(KRNL_STACK_PAGES, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);
    ptPages += CalcPagesToStoreEntries(gopBufferSize >> 12, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);

    UINT64 totalPages = pml4Pages + pdptPages + pdPages + ptPages;
    UINT64 itselfPages = CalcPagesToStoreEntries(totalPages, sizeof(PAGE_TABLE_ENTRY), PAGE_4KB);

    return totalPages + itselfPages + 1; 
}

static UINT64
KrnlStackPages()
{
    return KRNL_STACK_PAGES + KRNL_STACK_PAGES;
}

static void
CalcRequiredPages(ELF64_LOAD_INFO *loadInfo, uint64_t mapSize, uint64_t gopBufferSize, REQUIRED_PAGES_INFO *result)
{
    result->KernelCodePages = loadInfo->ExecPhysPages;
    result->KernelDataPages = loadInfo->DataPhysPages;
    result->KernelSpanPages = loadInfo->VirtSpanPages;
    result->HeaderPages = HeaderPackedPages(mapSize);
    result->PageTablePages = PageTablePages(result, gopBufferSize);
    result->TotalPages = result->KernelSpanPages + result->HeaderPages + result->PageTablePages + KrnlStackPages();
}

static STAGING_BLOCK *
GetStagingBlock(REQUIRED_PAGES_INFO *pagesInfo, UINT64 totalReclaimable)
{
    UINT64 basePhys = 0;

    EFI_STATUS status =
        g_ST->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, pagesInfo->TotalPages, &basePhys);
    if (EFI_ERROR(status))
    {
        ConsoleFormatWrite(L"Failed to allocate pages for staging block: %k (0x%x)\r\n", status, status);
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
    block->CoreLocalDataSize = sizeof(CPU_CORE_LOCAL_DATA);
    block->MemoryMapSize = (pagesInfo->HeaderPages << 12) - sizeof(STAGING_BLOCK) - sizeof(CPU_CORE_LOCAL_DATA);
    block->PageTableSize = pagesInfo->PageTablePages << 12;
    block->KrnlCodeSize = pagesInfo->KernelCodePages << 12;
    block->KrnlDataSize = pagesInfo->KernelDataPages << 12;
    block->KrnlVirtSize = pagesInfo->KernelSpanPages << 12;
    block->KrnlStackSize = KRNL_STACK_SIZE;

    block->CoreLocalDataPhys = basePhys + block->InfoSize;
    block->MemoryMapPhys = block->CoreLocalDataPhys + block->CoreLocalDataSize;
    block->PageTablePhys = block->MemoryMapPhys + block->MemoryMapSize;
    block->KrnlEntryPhys = block->PageTablePhys + block->PageTableSize;
    block->KrnlStackPhys = block->KrnlEntryPhys + block->KrnlDataSize + block->KrnlCodeSize;
    block->KrnlIST1StackPhys = block->KrnlStackPhys + block->KrnlStackSize;

    block->CoreLocalDataVirt = block->BaseVirt + block->InfoSize;
    block->MemoryMapVirt = block->CoreLocalDataVirt + block->CoreLocalDataSize;
    block->PageTableVirt = block->MemoryMapVirt + block->MemoryMapSize;
    block->KrnlEntryVirt = KRNL_ENTRY_VA;
    block->KrnlStackVirt = KRNL_STACK_VA;
    block->KrnlIST1StackVirt = KRNL_IST1_STACK_VA;

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

    ConsoleFormatWrite(L"Loading kernel to 0x%x (Size: %u byte)\r\n", (UINT64)params.Base, params.BaseSize);
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

    __asm__ __volatile__("mov %[rsp], %%rsp\n\t"
                         "pushq $0\n\t"
                         "mov %[arg], %%rdi\n\t"
                         "jmp *%[entry]\n\t"
                         : /* No output operands */
                         : [rsp] "r"(stackTopVirt), [arg] "r"(blockVirt), [entry] "r"(entryVirt)
                         : "rdi");
}