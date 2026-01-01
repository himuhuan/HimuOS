#include "bootloader.h"
#include "io.h"

void
efi_main(EFI_HANDLE imageHandle, struct EFI_SYSTEM_TABLE *systemTable)
{

    LoaderInitialize(imageHandle, systemTable);
    LOG_DEBUG("Loader located at %p\n", efi_main);
    EFI_STATUS status = StagingKernel(L"kernel.bin");
    if (EFI_ERROR(status))
    {
        LOG_ERROR(L"Kernel staging failed: %k (0x%x)\r\n", status, status);
        while (1)
            ;
    }

    asm volatile("cli; hlt");
}