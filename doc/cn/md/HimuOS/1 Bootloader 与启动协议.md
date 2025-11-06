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

如图所示，`Staging Block`的布局如下：



### 内核启动时的内存布局对照表

根据`CreateKrnlMapping` 函数，Bootloader 在将控制权交给内核前，创建了如下的虚拟内存映射。这个映射是内核运行的第一个虚拟地址空间。

| 物理分布 (Staging Block) | 大小 / 特性       | 对应虚拟地址 (Virtual Space)                       | 说明                                | 权限          |
| :------------------: | ------------- | -------------------------------------------- | --------------------------------- | :---------- |
|      低 4GB 物理内存      | 0 ~ 4GB       | `0x00000000 ~ 0xFFFFFFFF`                    | Identity-map，启动过渡和兼容性使用           | R-W-X       |
|      Boot Info       | 若干字节（结构体）     | `BaseVirt (0xFFFF800000000000)`              | 启动参数、命令行、内核属性等                    | R-W         |
|   UEFI Memory Map    | 依赖固件提供大小      | `MemoryMapVirt`                              | 从 UEFI 传入的内存布局                    | R-W         |
|      Page Table      | 页对齐 (4 KB 单元) | `PageTableVirt`                              | 启动时建立的分页结构                        | R-W         |
|        Kernel        | ELF 段大小（对齐）   | `KrnlEntryVirt (0xFFFF804000000000)`         | 内核映像（.text, .rodata, .data, .bss） | R-W-X       |
|     Kernel Stack     | 16 KB         | `KrnlStackVirt(0xFFFF808000000000)`          | 启动内核栈，RSP 指向此处栈顶                  | R-W         |
|      IST1 堆栈保护页      | 4KB           | `KRNL_STACK_VA + KRNL_STACK_SIZE`            | 紧随主堆栈之后，用于防止堆栈溢出到 IST1 堆栈         | Not Present |
|       IST1 堆栈        | 16 KB         | `KRNL_STACK_VA + KRNL_STACK_SIZE + PAGE_4KB` | 用于处理 Double Fault 等严重异常的特殊堆栈      | R-W         |
|    GOP 帧缓冲 (MMIO)    | -             | `FramebufferVirt(0xFFFFC00000000000)`        | MMIO 映射区，设备寄存器、PCI BAR 等          | R-W         |

1.  Properties 特指 `STAGING_BLOCK` 结构体本身可见的所有字段，包含内核启动所需的所有参数和信息，以及其它区域的物理和虚拟地址。
2. 特别地，Properties、UEFI Memory Map 和 Page Table 三部分共同构成了 `Boot Info` 区域。它们会被映射到同一块虚拟地址空间中。
3. Kernel、 Kernel Stack 和 Low 4GB Physical Memory 三部分则分别映射到各自的虚拟地址空间中。
4. RSP 将在被引导至内核时指向 Kernel Stack 顶 (`KrnlStackVirt + KrnlStackSize - 1`)

>[!Caution]
> HBL 禁用了所有页的缓存。旨在避免在内核内存管理器完全初始化前，因复杂的缓存一致性问题导致系统不稳定。内核在初始化其内存管理子系统后，会根据需要重新建立页表，并精细地控制各个内存区域的缓存策略（如设置为 Write-Back）以获得最佳性能。

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
