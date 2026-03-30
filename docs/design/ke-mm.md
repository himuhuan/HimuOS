# HimuOS Ke 层内存子系统实现说明

## 1. 文档目的

本文面向 HimuOS 内核开发者，说明当前分支 `feature/vmm-subsystem` 中，`Ke` 层如何把物理内存管理、导入后的内核地址空间、页表操作、KVA 分配器、临时物理页映射、页级 heap 与对象池串成一个可运行的最小内存子系统。

这套实现的目标不是一次性做完完整 VMM，而是在仍处于“单内核地址空间、单活动 CR3”的阶段，先稳定以下能力：

1. 接管 Boot 交付的物理页资源。
2. 把 Boot 已经安装好的页表导入为 `Ke` 层的一等对象。
3. 在导入页表之上，为内核提供受控的虚拟地址保留与 4KB 映射能力。
4. 为线程栈、fixmap、页级 heap、对象池和诊断接口提供统一的内存语义。

## 2. 总体分层

当前内存子系统可以理解为五层：

1. `KE_PMM`
   负责物理页生命周期，只管理“哪一页可分配、已分配、已保留”。
2. Imported kernel address space
   负责把 Boot 阶段已经存在的根页表和映射目录导入到 `Ke` 层。
3. PT HAL
   负责对 imported root 做最小页表操作：查询、4KB 建图、拆图、改保护。
4. `KE_KVA`
   负责管理内核虚拟地址 arena，并把“保留虚拟地址”和“提交物理页”拆开。
5. 上层 consumers
   包括线程栈、临时物理页映射、页级 heap、`KePool`、KTHREAD 池，以及 sysinfo/故障诊断路径。

职责边界也很明确：

- PMM 不负责虚拟地址。
- imported root/PT HAL 不负责分配策略。
- KVA 不负责多地址空间切换，也不负责用户态。
- heap 目前是页级接口，还不是通用小对象分配器。
- 对象池只复用页级 heap，不反向管理页表。

## 3. 启动顺序与依赖关系

当前 `InitKernel()` 里的内存初始化顺序已经被明确固定下来：

1. `KePmmInitFromBootMemoryMap(block)`
2. `KeImportKernelAddressSpace(block, gBootMappingManifest)`
3. `KePtSelfTest()`
4. `KeKvaInit()`
5. `KeKvaSelfTest()`
6. `RunMemoryObservabilitySelfTest()`
7. 依赖 heap foundation 的子系统初始化，例如 `KeKThreadPoolInit()`

这个顺序表达了当前实现的核心约束：

- PMM 必须先接管物理页，后面导入 region 目录和补充页表时都要继续申请物理页。
- imported address space 必须先建立，因为 KVA 需要验证自己的 arena 没有和已有导入映射重叠。
- PT HAL 先自检，确保最基本的页表读写能力可靠。
- KVA 再初始化，因为它依赖 imported root 的“空洞”来安放 stack/fixmap/heap arena。
- `KePool` 和 KTHREAD 池最后启动，因为 pool 扩容已经改为走 `KeHeapAllocPages()`。

## 4. PMM：Ke 层的物理页资源边界

`KE_PMM` 的实现位于 [`src/kernel/ke/pmm/pmm_boot_init.c`](/home/liuhuan/projects/HimuOS/src/kernel/ke/pmm/pmm_boot_init.c)，它基于 Boot 交付的 EFI memory map 建立位图型物理页管理器。

### 4.1 输入整理

初始化阶段会先对内存描述符做排序和校验：

- 按物理地址升序整理描述符。
- 拒绝非 4KB 对齐描述符。
- 检查回绕和重叠。
- 只把 reclaimable 区间纳入“可受管跨度”。

最终 PMM 不只记住“哪些 reclaimable 区间”，而是冻结一个 `ManagedBasePhys -> ManagedBasePhys + TotalManagedPages * 4KB` 的受管大跨度；跨度内不属于 reclaimable 的页会被再次标成 `RESERVED`。

### 4.2 元数据自举

PMM 的 bitmap 元数据本身也需要落在物理页上。当前实现会：

- 在 reclaimable 内存里寻找一段能容纳 bitmap 的物理连续区间。
- 显式避开 Boot 仍占用的区域。
- 新增保留了低 1MiB 窗口，不让 bitmap 放进 legacy low memory。
- 通过 HHDM 直接访问 bitmap 所在物理页并完成初始化。

### 4.3 状态模型

位图后端采用 2-bit 每页状态，核心状态是：

- `FREE`
- `ALLOCATED`
- `RESERVED`

初始化完成后，会再次保留：

- bitmap 元数据页
- 低 1MiB window
- page 0
- 内核镜像
- 主栈和 IST1 栈
- handoff block / memory map
- 当前页表页
- framebuffer

因此，PMM 提供的是“扣除 boot 仍持有实体后的物理页燃料”。

## 5. Imported kernel address space：把 Boot 页表提升为 Ke 对象

`KeImportKernelAddressSpace()` 实现在 [`src/kernel/ke/mm/address_space.c`](/home/liuhuan/projects/HimuOS/src/kernel/ke/mm/address_space.c)。

它做的事情不是“重建页表”，而是“认领现有活动根页表”：

1. 读取当前 CR3。
2. 与 Boot Capsule 中交接的 root page table 做一致性校验。
3. 从 `BOOT_MAPPING_MANIFEST` 导入 region 目录。
4. 为这些 `KE_IMPORTED_REGION` 申请目录存储页，并以 HHDM 访问填充。
5. 把 imported root、region 列表、boot-owned 计数、pinned 计数写入全局 `KE_KERNEL_ADDRESS_SPACE`。

当前阶段 imported address space 的关键特征是：

- 仍然使用 boot 已安装的 live root。
- 所有 imported region 先视为 `Pinned`。
- Boot-owned region 不能被悄悄丢失。
- region 查找选择“覆盖该地址的最小区间”，这样 HHDM 大窗口里的 overlay 仍可单独识别。

这一步让 `Ke` 层之后不必再反复依赖 boot handoff 结构，而是统一通过 `KeGetKernelAddressSpace()` 访问当前内核地址空间元数据。

## 6. PT HAL：在 imported root 上做最小页表操作

同一个文件中还实现了 phase-one 页表操作接口：

- `KePtQueryPage()`
- `KePtMapPage()`
- `KePtUnmapPage()`
- `KePtProtectPage()`
- `KePtSelfTest()`

### 6.1 当前支持范围

这套 HAL 是保守设计：

- 只支持 4KB leaf 的新增、删除和保护更新。
- 可以识别 2MB/1GB 大页覆盖，但不会主动拆分它们。
- 缺失中间页表时，会向 PMM 申请页表页并经 HHDM 初始化。
- 若当前 imported root 正处于活动 CR3，会执行本地 `invlpg`。

这意味着它更像“在现有内核 root 上打补丁”的最小 HAL，而不是完整 VMM。

### 6.2 自检策略

`KePtSelfTest()` 会：

1. 在高半区找一个没有 imported region 覆盖、也没有现存映射的 scratch hole。
2. 向 PMM 申请一个物理页。
3. 验证 query 结果最开始是 unmapped。
4. 建立 4KB 映射，检查 alias 读写。
5. 修改保护位，验证属性变化。
6. 解除映射，再确认 query 回到未映射。

这样，KVA 在接手之前，最底层页表读写路径已经被显式验证过。

## 7. KE_KVA：Ke 层虚拟地址管理器

`KE_KVA` 实现在 [`src/kernel/ke/mm/kva.c`](/home/liuhuan/projects/HimuOS/src/kernel/ke/mm/kva.c)，它是本分支最核心的新增。

它解决的不是“有没有页”，而是“内核里哪些稳定虚拟地址应由谁管理”。

### 7.1 三个固定 arena

当前 KVA 固定切出三个 arena：

1. `STACK`
2. `FIXMAP`
3. `HEAP`

布局特点：

- stack arena 放在 boot 主栈和 IST1 之后的高半区。
- heap arena 紧跟 stack arena。
- fixmap arena 放在 `HHDM_BASE_VA` 下沿附近，作为短生命周期别名窗口。

这些 arena 都是静态大小、静态位置，初始化时通过 `KiKvaValidateArena()` 检查：

- 是否和 imported region 重叠。
- 初始状态下是否已经被映射。

这保证了 KVA 不是“猜测某些地址大概空着”，而是显式验证自己占用的是 imported root 中真正的 hole。

### 7.2 记录模型

KVA 的内部账本由两部分组成：

- `KE_KVA_ARENA_STATE`
  记录 arena 基址、大小、页数、每页状态。
- `KE_KVA_RANGE_RECORD`
  记录一次具体分配的 range，包括 usable 页、guard 页和 ownership。

对外暴露的 `KE_KVA_RANGE` 则把一个分配区间表达为：

- `BaseAddress`
- `UsableBase`
- `TotalPages`
- `UsablePages`
- `GuardLowerPages`
- `GuardUpperPages`

这使“整段保留区间”和“真正可访问起点”被清楚地区分开，线程栈 guard page 因而能自然表达。

### 7.3 两阶段模型：先保留 VA，再决定是否提交 PA

`KeKvaAllocRange()` 只做一件事：

- 在指定 arena 中找一段连续空闲页；
- 把 usable 页标为 `ALLOC`，guard 页标为 `GUARD`；
- 建立 range record。

它并不会自动向 PMM 申请 backing page。

后续动作分两类：

1. 外部提供物理页
   调用 `KeKvaMapPage()` 把具体物理页映射到 range 的 usable page。
2. KVA 自己拥有物理 backing
   调用 `KeKvaMapOwnedPages()`，由 KVA 逐页 `KePmmAllocPages()` 并建立映射。

这个拆分是当前设计最重要的一点，因为它把：

- 虚拟地址连续
- 物理页是否连续
- 物理页归谁拥有

这三件事解耦了。

## 8. fixmap 与临时物理页映射

在旧模型里，运行期 RAM 很容易直接把 HHDM 当作“对象地址”使用；本分支开始显式收缩这种做法。

当前 fixmap 路径是：

1. `KeFixmapAcquire()`
   从 fixmap arena 保留 1 页 KVA range，并把指定物理页映射进去。
2. `KeTempPhysMapAcquire()`
   再在上层包一层 opaque handle，避免调用方直接持有 slot 编号。
3. `KeTempPhysMapRelease()`
   基于 token 中编码的 slot + generation 安全回收临时映射。

`generation` 追踪是本分支最后一个提交补上的能力，它让“旧 handle 释放了新一轮复用的 slot”这种 ABA 风险被显式拒绝。

当前语义是：

- fixmap 适合短生命周期 alias。
- slot 可复用，但 release 必须匹配当次 generation。
- runtime RAM 的临时访问优先走 temp map，而不是直接长期持有 HHDM 别名。

## 9. 页级 heap：KVA 管 VA，PMM 提供 backing

heap 目前还不是 malloc/slab，而是页级接口：

- `KeHeapAllocPages(pageCount, &virt)`
- `KeHeapFreePages(baseVirt)`

其内部路径很直白：

1. 从 heap arena 分配一个 `OwnsPhysicalBacking = TRUE` 的 KVA range。
2. 用 `KeKvaMapOwnedPages()` 逐页向 PMM 申请物理页并映射。
3. 释放时通过 `KeKvaReleaseRange()` 逆向 unmap 并释放物理页。

因此，heap 的真正价值在于提供“稳定的、非 HHDM 的、KVA 管理的页级内核地址”，而不是对象粒度接口。

## 10. KePool 与 KTHREAD：上层 consumer 的接入方式

### 10.1 KePool

[`src/kernel/ke/mm/pool.c`](/home/liuhuan/projects/HimuOS/src/kernel/ke/mm/pool.c) 的关键变化是：对象池扩容不再直接走 `KePmmAllocPages()`，而是改为走 `KeHeapAllocPages(1)`。

这意味着：

- pool backing page 拥有稳定虚拟地址。
- pool 不再把“页对象地址”建立在 HHDM 线性别名上。
- `KeKvaInit()` 变成对象池初始化的前置依赖。

当前 `KePoolFree()` 只把 slot 挂回 freelist，不会把整个 backing page 释放回 heap foundation；这是当前实现有意保留的“只增长、不 shrink”语义。

### 10.2 KTHREAD 栈

[`src/kernel/ke/thread/kthread.c`](/home/liuhuan/projects/HimuOS/src/kernel/ke/thread/kthread.c) 中，线程栈也已经切到 KVA：

1. 在线程创建时，从 stack arena 分配 `usable = KE_THREAD_STACK_PAGES`、`guardLower = 1` 的 range。
2. 用 `KeKvaMapOwnedPages()` 给 usable 页建立物理 backing。
3. `KTHREAD` 里记录：
   - `StackBase`
   - `StackSize`
   - `StackGuardBase`
   - `StackOwnedByKva`
4. 线程退出回收时，通过 `KeKvaReleaseRange(thread->StackBase)` 一次性回收栈虚拟区间与其物理 backing。

这一步的意义很大：

- 线程栈终于按“虚拟连续”建模，而不是“物理连续 + HHDM 别名”。
- guard page 成为显式语义。
- 线程栈生命周期与 KVA range 生命周期对齐。

## 11. 观测面与自检

这次分支的另一个重要变化是，它没有停在“能分配就算完成”，而是开始补诊断与统计。

### 11.1 sysinfo

`KeQuerySystemInformation()` 新增或补全了两类关键查询：

- `KE_SYSINFO_PHYSICAL_MEM_STATS`
  返回 PMM 的 `Total/Free/Allocated/Reserved` 四元统计。
- `KE_SYSINFO_VMM_OVERVIEW`
  返回 imported region 数量、stack/fixmap/heap 三个 arena 的总页数/空闲页数/活跃分配数，以及活跃 range 数和 fixmap 槽位使用量。

这样内存系统开始具备统一可查询的状态面，而不再只依赖零散日志。

### 11.2 启动期 observability self-test

`RunMemoryObservabilitySelfTest()` 会联动验证：

1. 先读取 PMM/VMM 基线统计。
2. 申请 1 个 PMM 物理页，并通过 `KeTempPhysMapAcquire()` 建立 fixmap alias。
3. 检查 fixmap 激活后，物理统计和 VMM 统计是否同步变化。
4. 释放临时映射与物理页。
5. 再做一次 `KeHeapAllocPages()`。
6. 检查 heap 分配是否推动 PMM/VMM 计数变化。
7. 最后回收资源并确认所有统计回归基线。

这让本分支第一次具备了“账本能否闭环”的自动验证。

### 11.3 页故障演示与基础异常信息

分支还增加了 page-fault demo：

- imported region fault
- guard page fault
- fixmap page fault
- heap page fault

同时在 CPU exception 输出中补了 `CR2` 和 `PFERR` 位信息，便于区分 fault 地址与 fault 类型。它们主要用于 bring-up 和 VMM 诊断演练。

## 12. 当前实现边界

当前 Ke 内存子系统已经形成最小闭环，但仍然是 phase-one 设计，边界非常明确：

1. 只有单内核地址空间
   还没有用户态地址空间、地址空间切换和 clone/import/export 机制。
2. 页表操作只覆盖 4KB leaf
   不能拆分 imported 的 2MB/1GB large page。
3. 没有多核 TLB shootdown
   当前只有活动 CR3 上的本地 `invlpg`。
4. KVA arena 是静态布局
   没有动态扩展、回收压缩或更复杂的 address-space policy。
5. heap 只有页级接口
   还不是通用堆，也没有分层 allocator/slab。
6. pool 只回收 slot，不回收 backing page
   当前没有 shrink/destroy。
7. 仍允许部分 HHDM 访问存在
   尤其是在 boot 交接、页表 helper、自检和某些诊断路径中，HHDM 仍是受控逃生口。

## 13. 后续演进建议

基于当前实现，后续最自然的演进方向包括：

1. 在 imported root 之上引入真正的 address-space 对象生命周期。
2. 支持 large page 识别之外的拆分、重映射和更细粒度保护。
3. 为 KVA 引入更丰富的区间管理策略，而不只是一维 first-fit。
4. 在 heap 之上再构建通用 kernel heap / slab 分配器。
5. 把 page-fault 路径进一步接上 `KeDiagnoseVirtualAddress()`，把 imported/PT/KVA 三层诊断真正打进异常输出。
6. 在 SMP 阶段补齐跨核同步和 TLB shootdown。

## 14. 小结

`feature/vmm-subsystem` 的核心成果，不是简单“新增几个内存 API”，而是把 HimuOS `Ke` 层的内存路径重组为一条清晰链路：

`Boot memory map -> PMM -> imported kernel address space -> PT HAL -> KVA -> fixmap/heap/pool/thread stack -> sysinfo/self-test`

到这一步，HimuOS 已经具备了一个可解释、可观测、可继续扩展的内核内存骨架。后续无论是做更完整的 VMM、kernel heap，还是把异常诊断与用户态内存模型接上，都会明显比之前“PMM + HHDM 直用”的阶段更稳。
