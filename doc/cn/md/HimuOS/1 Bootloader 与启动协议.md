# Bootloader 与启动协议

## 概述 (Overview)

本文档旨在精确定义HimuOS启动管理器（Bootloader）与内核（Kernel）之间的接口规范。
它详细描述了内核镜像加载到物理内存的布局（Staging Block） ，以及内核启动后所处的虚拟地址空间（Virtual Space）的架构。
遵循此规范是确保内核能被正确加载并启动的先决条件。

## 物理内存布局：Staging Block

为了将所有必要信息从`Bootloader`传递给内核，`Bootloader`会在物理内存的低地址区域分配一块连续的内存块，
称为`Staging Block`。这块内存不仅是传递信息的容器，也直接存放了内核的物理镜像和初始栈。

### 目的与分配

- 目的: 作为一个单一、连续的实体，简化物理内存管理和信息交接。
- 分配: Bootloader 负责找到并分配这块内存。其物理基地址不固定，但必须是页对齐的。 内核启动后，通过传递的`STAGING_BLOCK`
  结构体获知其确切位置和大小。

### 内部结构

```c
/**
 * STAGING_BLOCK
 *
 * The "Staging Block" is a temporary and contiguous area in memory used
 * during the very early stages of the kernel's boot process. It acts as an intermediate workspace
 * where the initial kernel code runs to set up the final virtual memory map.
 *
 * Staging Block starts with the `STAGING_BLOCK` structure, more details see also `doc`.
 *
 * The bootloader creates the page tables that establish the "Lower 4GB Identity Mapping" and map
 * this staging block to a higher-half address. The kernel then switches to this higher-half
 * address space and continues its initialization from there.
 *
 * All offsets in this structure are relative to the start of the staging block.
 */
typedef struct
{
    uint64_t Magic;               // 'HOS!' (0x214F5348)
    HO_PHYSICAL_ADDRESS BasePhys; // Physical base address of the staging block
    HO_VIRTUAL_ADDRESS BaseVirt;  // Virtual base address of the staging block
    uint64_t Size;                // Total size of the staging block
    uint64_t TotalReclaimableMem; // Total size of reclaimable memory

    // GOP
    HO_VIRTUAL_ADDRESS FramebufferBase;
    uint64_t FramebufferSize;
    uint64_t HorizontalResolution;
    uint64_t VerticalResolution;

    uint64_t HeaderSize;    // Size of header `STAGING_BLOCK`
    uint64_t MemoryMapSize; // Size of the memory map
    uint64_t PageTableSize; // Size of the page tables
    uint64_t KrnlMemSize;   // Size of the kernel ELF loaded segments
    uint64_t KrnlVirtSize;  // Size of the kernel virtual address space occupied by the kernel ELF loaded segments
    uint64_t KrnlStackSize; // Size of the kernel stack, always `KRNL_STACK_SIZE`

    HO_PHYSICAL_ADDRESS MemoryMapPhys; // Physical address of the memory map
    HO_PHYSICAL_ADDRESS KrnlEntryPhys; // Physical address of the kernel loaded segments
    HO_PHYSICAL_ADDRESS KrnlStackPhys; // Physical address of the kernel stack
    HO_PHYSICAL_ADDRESS PageTablePhys; // Physical address of the page tables
    
    HO_VIRTUAL_ADDRESS MemoryMapVirt; // Virtual address of the memory map
    HO_VIRTUAL_ADDRESS PageTableVirt; // Virtual address of the page tables
    HO_VIRTUAL_ADDRESS KrnlEntryVirt; // Virtual address of the entry of kernel
    HO_VIRTUAL_ADDRESS KrnlStackVirt; // Virtual address of the kernel stack
} STAGING_BLOCK;
```

如图所示，`Staging Block`的布局如下：

![himuos_kernel.drawio.png](resources/himuos_kernel.drawio.png#pic_center|)

### 内核启动时的内存布局对照表

| 物理分布 (Staging Block) | 大小 / 特性           | 对应虚拟地址 (Virtual Space)               | 说明                                |
| :------------------: | ----------------- | ------------------------------------ | --------------------------------- |
|      Properties      | 若干字节（结构体）         | `BaseVirt (0xFFFF800000000000)`      | 启动参数、命令行、内核属性等                    |
|   UEFI Memory Map    | 依赖固件提供大小          | `MemoryMapVirt`                      | 从 UEFI 传入的内存布局                    |
|      Page Table      | 页对齐 (4 KiB 单元)    | `PageTableVirt`                      | 启动时建立的分页结构                        |
|        Kernel        | ELF 段大小（对齐）       | `KrnlEntryVirt (0xFFFF804000000000)` | 内核映像（.text, .rodata, .data, .bss） |
|     Kernel Stack     | 固定 24 KiB         | `KrnlStackVirt(0xFFFF808000000000)`  | 启动内核栈，RSP 指向此处栈顶                  |
|       （高地址）RSP       | 指向 Kernel Stack 顶 | `KrnlStackVirt + KrnlStackSize - 1`  | 栈顶指针                              |
|     *(未在物理区体现)*      | -                 | `0xFFFFC00000000000`                 | MMIO 映射区，设备寄存器、PCI BAR 等          |
|      低 4GB 物理内存      | 0 ~ 4GB           | `0x00000000 ~ 0xFFFFFFFF`            | Identity-map，启动过渡和兼容性使用           |

 Properties 特指 `STAGING_BLOCK` 结构体本身可见的所有字段，包含内核启动所需的所有参数和信息，以及其它区域的物理和虚拟地址。

特别地，Properties、UEFI Memory Map 和 Page Table 三部分共同构成了 `Boot Info` 区域。它们会被映射到同一块虚拟地址空间中。

Kernel、 Kernel Stack 和 Low 4GB Physical Memory 三部分则分别映射到各自的虚拟地址空间中。

## HimuOS 内核镜像的 ELF 文件格式要求

HimuOS 的内核文件格式为 ELF 格式，内核文件名为 `kernel.bin`。内核在此基础上必须满足：

1. 内核必须是 x86_64 架构的合法 ELF 文件。
2. 内核必须是可执行文件（ELF Type 为 EXEC）。
3. 内核的所有 `PT_LOAD` 段的起始地址必须位于内核基址 `0xFFFF804000000000` 之上。
4. 最高的 `PT_LOAD` 段的结束地址必须小于 `0xFFFF808000000000`。
5. 内核的入口点必须是 `kmain(STAGING_BLOCK*)` 函数。
6. 只支持小端
7. 加载器只处理 `PT_LOAD` 段，其他段会被忽略。`HimuOS` 不会包含任何动态链接段。
8. 除非必要，`PT_LOAD` 段的数量应该尽可能少，且各段除必要的对齐要求外，应该尽可能连续。

以上条件有任一不满足，启动器将拒绝加载内核。
