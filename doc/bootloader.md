# Himu Operating System Doc

## Bootloader

### Memory initialization

HOBoot's allocator always allocates a large, contiguous, page-aligned block of `EfiLoaderData` for the upcoming data to be passed. The data structures that HOBoot passes to the kernel are always contiguous, including forced memory-aligned space, and are distributed sequentially in the order they were requested. The built-in strategy is very simple: it just returns the cumulative allocation offset and enforces alignment according to the align parameter in the `HAlloc` interface. HOBoot guarantees that the first structure in this memory region is always the `BootInfo` structure, and passes its address when booting the kernel.

HOBoot first scans the UEFI memory map to identify all `ReclaimableMemory`, which will form the `KernelMemoryPool` after boot. It then allocates a suitable `StagingArea` from `EfiConventionalMemory` to load and execute the kernel. Once running, the kernel will initialize its memory manager (MM) with the `KernelMemoryPool` and relocate itself from the `StagingArea` to its final destination.

The base address of kernel entry don't be determined by the bootloader, but by the kernel itself. The bootloader only provides a `StagingArea` for the kernel to load and execute. The kernel will then determine its own base address and relocate itself accordingly. Anyway, HimuOS always expected to relocate itself at `0x1000000` (16MB) if `0x1000000` is available, otherwise it will use the next available address that is aligned to 1MB.

