#include "ho_balloc.h"


HO_KERNEL_API HO_NODISCARD EFI_STATUS
HobAllocCreate(UINT64 baseAddress, HOB_BALLOC *allocator, UINTN nPage)
{
    memset(allocator, 0, sizeof(HOB_BALLOC));
    allocator->Base = baseAddress;
    allocator->TotalSize = nPage << 12;
    memset((void *)allocator->Base, 0, allocator->TotalSize);
    return EFI_SUCCESS;
}

HO_KERNEL_API HO_NODISCARD void *
HobAlloc(HOB_BALLOC *allocator, UINTN size, UINTN align)
{
    UINTN mis;

    if (!align || ((mis = (align & (align - 1)) != 0)))
    {
        allocator->ErrorCode = EFI_INVALID_PARAMETER;
        return NULL;
    }

    if (allocator->Offset > allocator->TotalSize - size)
    {
        allocator->ErrorCode = EFI_OUT_OF_RESOURCES;
        return NULL;
    }

    UINTN base = (UINT64)allocator->Base + allocator->Offset;
    UINTN pad = mis ? (align - mis) : 0;
    if (pad > allocator->TotalSize - allocator->Offset)
    {
        allocator->ErrorCode = EFI_OUT_OF_RESOURCES;
        return NULL;
    }

    UINT64 newOffset = allocator->Offset + pad;
    if (size > allocator->TotalSize - newOffset)
    {
        allocator->ErrorCode = EFI_OUT_OF_RESOURCES;
        return NULL;
    }

    allocator->Offset = newOffset + size;
    return (void *)(base + pad);
}

HO_KERNEL_API HO_NODISCARD UINTN
HobRemaining(const HOB_BALLOC *allocator)
{
    return allocator->TotalSize - allocator->Offset;
}

HO_KERNEL_API HO_NODISCARD BOOL
HobAllocError(const HOB_BALLOC *allocator)
{
    return EFI_ERROR(allocator->ErrorCode);
}
