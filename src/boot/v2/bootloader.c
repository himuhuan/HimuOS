#include "bootloader.h"
#include "io.h"
#include "arch/amd64/asm.h"
#include "arch/amd64/pm.h"
#include "arch/amd64/efi_mem.h"
#include "blmm.h"
#include "kernel/hodefs.h"
#include "lib/elf/elf.h"
#include "kernel/ke/mm.h"

// ===========================================
// Global variables

struct EFI_SYSTEM_TABLE *g_ST;

struct EFI_GRAPHICS_OUTPUT_PROTOCOL *g_GOP;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *g_FSP;

EFI_HANDLE gImageHandle;

// ==========================================

static BOOL LoadKernel(void *image, UINT64 imageSize, STAGING_BLOCK *staging);
static void JumpToKernel(STAGING_BLOCK *block);
static HO_PHYSICAL_ADDRESS FindAcpiRsdpPhys(void);
static BOOL IsGuidEqual(const struct EFI_GUID *left, const struct EFI_GUID *right);
static BOOL CpuSupportsNx(void);
static BOOL EnableNxe(void);

static BOOL kBootUseNx = FALSE;

BOOL
BootUseNx(void)
{
    return kBootUseNx;
}

void
LoaderInitialize(EFI_HANDLE imageHandle, struct EFI_SYSTEM_TABLE *SystemTable)
{
    static struct EFI_GUID gop_guid = {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};
    static struct EFI_GUID sfsp_guid = {0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};
    EFI_STATUS status;

    g_ST = SystemTable;
    g_ST->BootServices->SetWatchdogTimer(0, 0, 0, NULL);
    g_ST->ConsoleOutput->ClearScreen(g_ST->ConsoleOutput);
    ConsoleFormatWrite(L"HimuOS UEFI Boot Manager Initializing...\r\n");

    if ((status = g_ST->BootServices->LocateProtocol(&gop_guid, NULL, (void **)&g_GOP)) != EFI_SUCCESS)
    {
        LOG_ERROR(L"Failed to locate Graphics Output Protocol: %k (0x%x)\r\n", status, status);
        return;
    }

    if ((status = g_ST->BootServices->LocateProtocol(&sfsp_guid, NULL, (void **)&g_FSP)) != EFI_SUCCESS)
    {
        LOG_ERROR(L"Failed to locate Simple File System Protocol: %k (0x%x)\r\n", status, status);
        return;
    }

    gImageHandle = imageHandle;
}

EFI_STATUS
StagingKernel(const CHAR16 *path)
{
    LOG_INFO(L"Loading kernel from: %s\r\n", path);

    EFI_STATUS status = EFI_SUCCESS;
    void *image = NULL;
    EFI_MEMORY_MAP *memoryMap = NULL;
    UINT64 imageSize = 0;

    status = ReadKernelImage(path, &image, &imageSize);
    if (EFI_ERROR(status))
    {
        LOG_ERROR(L"Failed to read kernel image: %k (0x%x)\r\n", status, status);
        goto handle_error;
    }
    LOG_DEBUG(L"Kernel image size: %u bytes\r\n", imageSize);
    UINTN imagePages = HO_ALIGN_UP(imageSize, PAGE_4KB) >> 12;

    memoryMap = GetLoaderRuntimeMemoryMap();
    if (memoryMap == NULL)
    {
        status = EFI_OUT_OF_RESOURCES;
        LOG_ERROR(L"Failed to get memory map: %k (0x%x)\r\n", status, status);
        goto handle_error;
    }
    // +1 for safety margin
    UINTN memoryMapPages = (HO_ALIGN_UP(memoryMap->Size, PAGE_4KB) >> 12) + 1;

    ELF64_LOAD_INFO elfInfo;
    memset(&elfInfo, 0, sizeof(ELF64_LOAD_INFO));
    if (!Elf64GetLoadInfo(image, imageSize, &elfInfo))
    {
        LOG_ERROR(L"Invalid ELF64 kernel image\r\n");
        status = EFI_LOAD_ERROR;
        goto handle_error;
    }
    if (elfInfo.MinAddrVirt != elfInfo.EntryVirt || elfInfo.MinAddrVirt != KRNL_BASE_VA)
    {
        LOG_ERROR(L"FATAL: Non-standard kernel base address or entry point\r\n");
        status = EFI_UNSUPPORTED;
        goto handle_error;
    }

    BOOT_CAPSULE_LAYOUT layout;
    memset(&layout, 0, sizeof(layout));
    layout.HeaderSize = sizeof(BOOT_CAPSULE);
    layout.MemoryMapSize = memoryMapPages << 12;
    layout.KrnlCodeSize = elfInfo.ExecPhysPages << 12;
    layout.KrnlDataSize = elfInfo.DataPhysPages << 12;
    layout.KrnlStackSize = layout.IST1StackSize = HO_STACK_SIZE;
    BOOT_CAPSULE *capsule = CreateCapsule(&layout);
    if (capsule == NULL)
    {
        status = EFI_OUT_OF_RESOURCES;
        goto handle_error;
    }

    capsule->AcpiRsdpPhys = FindAcpiRsdpPhys();
    if (capsule->AcpiRsdpPhys == 0)
    {
        LOG_WARNING(L"ACPI RSDP not found in UEFI configuration tables\r\n");
    }

    kBootUseNx = CpuSupportsNx();
    if (!kBootUseNx)
        LOG_WARNING(L"CPU does not support NX bit; boot mappings will be executable\r\n");

    size_t remain = CreateInitialMapping(capsule);
    if (!remain)
    {
        LOG_ERROR(L"Failed to create initial page table mappings\r\n");
        status = EFI_OUT_OF_RESOURCES;
        goto handle_error;
    }

    (void)InitCpuCoreLocalData(&capsule->CpuInfo, sizeof(capsule->CpuInfo));

    if (!LoadKernel(image, imageSize, capsule))
    {
        LOG_ERROR(L"Failed to load kernel image into memory\r\n");
        status = EFI_LOAD_ERROR;
        goto handle_error;
    }

    PRINT_CAPSULE(capsule);

    LOG_INFO("Starting HimuOS...\r\n");
    UINTN memoryMapKey;
    status = LoadMemoryMap(capsule->MemoryMapPhys, capsule->Layout.MemoryMapSize, &memoryMapKey);
    if (status != EFI_SUCCESS)
    {
        LOG_ERROR(L"Failed to load memory map: %k (0x%x)\r\n", status, status);
        return status;
    }

    status = g_ST->BootServices->ExitBootServices(gImageHandle, memoryMapKey);
    if (EFI_ERROR(status))
    {
        LOG_ERROR(L"Failed to exit boot services: %k (0x%x)\r\n", status, status);
        return status;
    }

    if (kBootUseNx && !EnableNxe())
    {
        LOG_ERROR(L"Failed to enable IA32_EFER.NXE\r\n");
        return EFI_DEVICE_ERROR;
    }
    __asm__ __volatile__("cli" ::: "memory");
    LoadCR3(capsule->PageTableInfo.Ptr);
    LoadGdtAndTss(&capsule->CpuInfo);
    JumpToKernel(capsule);

handle_error:
    if (image)
        (void)g_ST->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)image, imagePages);
    if (memoryMap)
        (void)g_ST->BootServices->FreePages((EFI_PHYSICAL_ADDRESS)memoryMap, memoryMapPages);
    if (EFI_ERROR(status))
    {
        LOG_ERROR(L"Kernel staging failed, terminating boot process.\r\n");
        return status;
    }

    // Impossible to reach here
    return EFI_SUCCESS;
}

static BOOL
LoadKernel(void *image, UINT64 imageSize, STAGING_BLOCK *staging)
{
    ELF64_LOAD_BUFFER_PARAMS params;
    memset(&params, 0, sizeof(ELF64_LOAD_BUFFER_PARAMS));
    params.BaseVirt = KRNL_BASE_VA;
    params.Image = image;
    params.ImageSize = imageSize;
    params.Base = (void *)staging->KrnlEntryPhys;
    params.BaseSize = staging->Layout.KrnlCodeSize + staging->Layout.KrnlDataSize;
    LOG_INFO(L"Loading kernel to 0x%x (Size: %u byte)\r\n", (UINT64)params.Base, params.BaseSize);
    return Elf64LoadToBuffer(&params);
}

static void
JumpToKernel(STAGING_BLOCK *block)
{
    HO_VIRTUAL_ADDRESS stackTopVirt = KRNL_STACK_VA + block->Layout.KrnlStackSize;
    HO_VIRTUAL_ADDRESS blockVirt = HHDM_PHYS2VIRT(block->BasePhys);
    HO_VIRTUAL_ADDRESS entryVirt = KRNL_BASE_VA;

    __asm__ __volatile__("mov %0, %%rsp" ::"r"(stackTopVirt) : "memory");
    __asm__ __volatile__("pushq $0" ::: "memory");
    __asm__ __volatile__("mov %0, %%rdi" ::"r"(blockVirt) : "rdi", "memory");
    __asm__ __volatile__("jmp *%0" ::"r"(entryVirt));

}

static BOOL
CpuSupportsNx(void)
{
    uint32_t eax, ebx, ecx, edx;
    cpuid(0x80000000U, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000001U)
        return FALSE;

    cpuid(0x80000001U, &eax, &ebx, &ecx, &edx);
    return (edx & (1U << 20)) != 0;
}

static BOOL
EnableNxe(void)
{
    uint64_t efer = rdmsr(IA32_EFER_MSR);
    efer |= IA32_EFER_NXE;
    wrmsr(IA32_EFER_MSR, efer);
    return (rdmsr(IA32_EFER_MSR) & IA32_EFER_NXE) != 0;
}

static HO_PHYSICAL_ADDRESS
FindAcpiRsdpPhys(void)
{
    static struct EFI_GUID acpi20Guid = {0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};
    static struct EFI_GUID acpi10Guid = {0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d}};

    if (g_ST == NULL || g_ST->ConfigurationTable == NULL || g_ST->NumberOfTableEntries == 0)
        return 0;

    UINTN index = 0;
    for (index = 0; index < g_ST->NumberOfTableEntries; ++index)
    {
        EFI_CONFIGURATION_TABLE *table = &g_ST->ConfigurationTable[index];
        if (IsGuidEqual(&table->VendorGuid, &acpi20Guid))
        {
            return (HO_PHYSICAL_ADDRESS)(UINTN)table->VendorTable;
        }
    }

    for (index = 0; index < g_ST->NumberOfTableEntries; ++index)
    {
        EFI_CONFIGURATION_TABLE *table = &g_ST->ConfigurationTable[index];
        if (IsGuidEqual(&table->VendorGuid, &acpi10Guid))
        {
            return (HO_PHYSICAL_ADDRESS)(UINTN)table->VendorTable;
        }
    }

    return 0;
}

static BOOL
IsGuidEqual(const struct EFI_GUID *left, const struct EFI_GUID *right)
{
    if (left == NULL || right == NULL)
        return FALSE;

    if (left->Data1 != right->Data1 || left->Data2 != right->Data2 || left->Data3 != right->Data3)
        return FALSE;

    for (int i = 0; i < 8; ++i)
    {
        if (left->Data4[i] != right->Data4[i])
            return FALSE;
    }

    return TRUE;
}
