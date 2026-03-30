# KE_KVA 设计与实现文稿

## 概述与设计目标

`KE_KVA` 是 `Ke` 层在现有 PMM 与 imported root page table 之上建立的一层内核虚拟地址资源管理器。它的目标不是取代 PMM，也不是一次性完成完整 VMM，而是在当前单内核地址空间阶段，为“需要稳定虚拟地址、但不必依赖物理连续性”的内核资源提供统一分配语义。此次设计直接覆盖了三类典型对象：

1. **Kernel thread stack**：线程需要连续的栈虚拟地址和明确的栈顶语义，但并不要求底层物理页连续。
2. **Fixmap**：内核需要临时把某个物理页映射到一个短生命周期的虚拟槽位，用于受控访问。
3. **Page-backed heap foundation**：为后续更高层堆对象系统提供“按页增长、按页释放”的底层虚拟地址与物理页绑定能力。

在改动前，`KTHREAD` 栈直接依赖 PMM 返回的一段连续物理页，并通过 HHDM 直接把物理地址解释为虚拟地址使用。这种方式实现简单，但它把“线程栈需要连续虚拟空间”错误地约束成了“线程栈必须使用连续物理页”，也无法自然引入 guard page、fixmap 或统一的页级堆增长接口。`KE_KVA` 的引入，本质上是在现有地址空间导入与页表 HAL 已经可用的前提下，把“虚拟地址保留”和“物理页绑定”两个动作分离开。

因此，`KE_KVA` 当前阶段冻结的职责边界是：

- 管理一组预留的、内核专用的虚拟地址 arena。
- 在 arena 内部分配连续虚拟页范围，并记录 guard / usable / ownership 元数据。
- 通过现有 `KePtMapPage` / `KePtUnmapPage` 与 PMM 协作，为这些范围按需建立或撤销 4KB 映射。
- 对外导出线程栈、fixmap 和 page-backed heap 的最小可用契约。

它当前**不**负责：

- 用户态地址空间。
- 跨地址空间共享策略。
- 跨核 TLB shootdown。
- 多处理器并发同步。
- 通用对象堆、slab、vm object 或懒映射。

## 地址空间布局与 arena 划分

当前实现把 KVA 划分为三个固定 arena，它们都由 `kva.c` 中的常量直接定义：

1. **Stack arena**
2. **Heap arena**
3. **Fixmap arena**

布局上，`stack arena` 起始于 boot 时内核主栈与 `IST1` 栈之后，并向高地址方向预留一段连续虚拟区间；`heap arena` 紧随 `stack arena` 之后，同样位于高半内核专用区；`fixmap arena` 则被放置在 `HHDM_BASE_VA` 之前的一个小窗口中。

这种布局对应了三种不同的目标：

- **Stack arena** 紧邻现有 boot stack 区域，便于在概念上把“早期固定内核栈”向“运行期动态线程栈”平滑过渡，同时可为每个线程保留 guard page。
- **Heap arena** 与 stack arena 分离，避免线程栈和页式堆在同一子池中竞争，降低生命周期不同的资源彼此碎片化影响。
- **Fixmap arena** 放在 `HHDM` 下沿附近，是因为 fixmap 本质上是“短生命周期单页别名窗口”，它需要一个稳定、可枚举、易于回收的槽位区，而不应混入长期存在的栈或堆地址范围。

当前尺寸配置为：

- `stack arena`: `32 MiB`
- `heap arena`: `64 MiB`
- `fixmap arena`: `64 * 4 KiB`

这些大小都属于第一阶段静态冻结参数，其核心目的是先稳定语义和边界，而不是追求动态可伸缩。

## 分层关系：PMM、Imported Root 与 KE_KVA

`KE_KVA` 依赖于两项已经建立的底层能力：

1. **PMM 已经接管物理页资源**：`KePmmAllocPages` / `KePmmFreePages` 提供物理页分配与回收。
2. **Imported root page table 已经变成一等对象**：`KeImportKernelAddressSpace` 把 boot 阶段构造的活动页表导入为 `KE_KERNEL_ADDRESS_SPACE`，并由 `KePtQueryPage` / `KePtMapPage` / `KePtUnmapPage` 提供 4KB leaf 级别的操作。

因此，`KE_KVA` 不是独立的页表管理器，而是一个位于两者之间的组装层：

- 它先在虚拟地址层面保留一段 range。
- 再决定这段 range 是否需要物理页。
- 若需要，则逐页向 PMM 申请物理页并映射到 imported root。
- 释放时，则撤销映射并在需要时把物理页归还 PMM。

这种关系意味着 `KE_KVA` 能够把“虚拟连续”与“物理连续”解耦，同时复用已有页表 HAL，而无需重新定义更高层的 VMM 抽象。

## 核心数据结构与内部状态机

### Arena 类型

对外，`KE_KVA_ARENA_TYPE` 定义了当前可见的三类 arena：

- `KE_KVA_ARENA_STACK`
- `KE_KVA_ARENA_FIXMAP`
- `KE_KVA_ARENA_HEAP`

这一枚举是所有分配接口的第一层路由键。调用方必须显式声明“我要在哪一种语义的虚拟地址池里分配”，从而让设计保持可解释性，而不是提供一个无语义的大地址池。

### 对外 range 描述

`KE_KVA_RANGE` 是对外暴露的已分配区间描述符。它包含：

- `Arena`
- `RecordId`
- `BaseAddress`
- `UsableBase`
- `TotalPages`
- `UsablePages`
- `GuardLowerPages`
- `GuardUpperPages`

这里最关键的语义区分在于：

- `BaseAddress` 是整个范围的起点，包含 guard page。
- `UsableBase` 才是调用方真正可以访问的可用起点。

也就是说，`KE_KVA_RANGE` 明确把“保留的虚拟区间”和“可用区间”区分开，线程栈等对象可以据此自然表达“低端 guard + 高端 usable”的布局。

### Arena 运行时状态

内部使用 `KE_KVA_ARENA_STATE` 管理每个 arena 的静态属性：

- 名称
- 基址
- 字节大小
- 页数
- `PageStates`

`PageStates` 是每个 arena 的页粒度真相源。当前实现中每页只有三种状态：

- `FREE`
- `ALLOC`
- `GUARD`

这里的 `GUARD` 并不表示“已映射但带保护属性”，而是表示“该页属于已保留 range 的 guard 区，不允许作为可用页再次分配，且通常保持未映射”。

### Range 记录

内部的 `KE_KVA_RANGE_RECORD` 维护了真正的分配账本，包含：

- `InUse`
- `OwnsPhysicalBacking`
- `Arena`
- `RecordId`
- `BasePageIndex`
- `TotalPages`
- `UsablePages`
- `GuardLowerPages`
- `GuardUpperPages`

其中 `OwnsPhysicalBacking` 很重要。它区分了两类 range：

1. **KVA 只管理虚拟区间，不拥有物理页**：例如 fixmap，物理页由调用方提供。
2. **KVA 同时拥有虚拟区间与物理页生命周期**：例如线程栈和 page-backed heap。

这一位让同一套 range 管理机制可以同时覆盖 fixmap 和 heap/stack，而不需要为每类对象维护完全不同的释放逻辑。

## 初始化与布局校验

`KeKvaInit` 的职责不是分配任何业务对象，而是把 KVA 的 arena 元数据和内部记录表初始化为可用状态，并对地址布局做一次早期合法性验证。

它主要完成以下工作：

1. 清零所有 `gKvaRanges` 和各 arena 的 `PageStates`。
2. 根据预定义常量建立 `stack` / `fixmap` / `heap` 三个 arena 的运行时描述。
3. 为每个 range record 预先写入稳定的 `RecordId`。
4. 调用 `KiKvaValidateArena` 校验每个 arena：
   - 不与 imported boot mappings 重叠。
   - 在初始化时要求整段虚拟区间目前均未被映射。

这一点非常关键：`KE_KVA` 没有假设“这些地址大概空着”，而是显式向 imported root 询问映射状态，确认这些 arena 真的是后续可支配的 hole。这样它与 boot 阶段保留下来的 HHDM、内核镜像、栈区、framebuffer 等导入区域形成了清晰边界。

## 分配模型：先保留虚拟范围，再建立映射

`KeKvaAllocRange` 是 KVA 的核心入口。它本质上做两件事：

1. 从目标 arena 中寻找一段连续空闲虚拟页。
2. 将其标记为一个逻辑 range，并写入 range record。

它**不自动映射物理页**。调用方需要根据用途决定后续动作：

- 如果这是 fixmap，可以调用 `KeKvaMapPage` 把指定物理页映射进去。
- 如果这是 KVA 自己拥有 backing 的对象，可以调用 `KeKvaMapOwnedPages` 让 KVA 自己向 PMM 逐页申请物理页并建立映射。

这种两阶段模型有几个直接收益：

- 分离“地址保留”和“物理页提交”。
- 支持 guard page 存在但不映射。
- 允许 fixmap 这类“虚拟空间由 KVA 管，物理页由外部决定”的用法。

从状态变化上看，一个 range 的生命周期大致是：

1. `FREE` 页被挑选出来。
2. guard 页被标记为 `GUARD`，usable 页被标记为 `ALLOC`。
3. range record 进入 `InUse`。
4. 可选地，为 usable 页建立 4KB 映射。
5. 释放时，解除映射并把页状态恢复为 `FREE`。

## 核心 API 契约

### `KeKvaInit`

初始化全局 KVA 子系统。它要求 imported kernel address space 已经成功导入，否则无法验证 arena 是否与现有导入映射重叠。初始化成功后，KVA 的所有 arena 才可对外分配。

### `KeKvaAllocRange`

在指定 arena 中保留一段连续虚拟页范围，支持分别指定：

- `usablePages`
- `guardLowerPages`
- `guardUpperPages`
- `ownsPhysicalBacking`

其结果是一个 `KE_KVA_RANGE`。此时 usable 页可能尚未映射。

### `KeKvaMapPage`

把 range 中某个 usable 页索引映射到指定物理页。该接口适合 fixmap 或更通用的外部物理页绑定场景。它依赖 imported root page table HAL，在当前阶段只支持 4KB leaf 映射。

### `KeKvaMapOwnedPages`

针对 `OwnsPhysicalBacking = TRUE` 的 range，由 KVA 自行逐页向 PMM 申请物理页并映射。若中途任何一步失败，它会回滚已经建立的部分映射并释放整个 range。这样调用方不需要自己逐页清理半成品状态。

### `KeKvaReleaseRange`

按 `usableBase` 查找 range，并回收其全部 usable 页映射；如果该 range 拥有物理 backing，则同时把物理页归还 PMM。最后，它会把 guard 与 usable 页统一恢复为 `FREE`，并清空内部 record。

### `KeKvaQueryRange`

依据 `usableBase` 反查一个 range 的对外描述，用于调试、状态查询或对象回收前校验。

### `KeKvaQueryArenaInfo`

查询某个 arena 的总体信息，包括：

- 起止地址
- 总页数
- 空闲页数
- 活跃分配数
- 是否与 imported regions 重叠

该接口主要用于可观测性与后续调试工具对接。

### `KeKvaValidateLayout`

重新验证 arena 布局是否仍与 imported boot mappings 保持不重叠。它更像是一个一致性检查入口，而不是完整的在线审计器。

### `KeKvaSelfTest`

执行一组 boot-time 自测，验证 KVA 当前冻结契约是否成立，包括 guard page、fixmap 和 page-backed heap 的基本行为。

### `KeFixmapAcquire` / `KeFixmapRelease`

这是 fixmap 的专用包装接口：

- `Acquire` 在 fixmap arena 中申请一个单页虚拟槽位。
- 然后把调用方提供的物理页映射到该槽位。
- `Release` 依据 slot 撤销映射并回收槽位。

它为上层提供的是“稳定 API + 简单 slot 语义”，而不是暴露底层 range record。

### `KeHeapAllocPages` / `KeHeapFreePages`

这是 page-backed heap foundation 的最小包装：

- `AllocPages` 在 heap arena 中申请一段连续虚拟页。
- KVA 自行向 PMM 逐页申请 backing 并建立映射。
- `FreePages` 释放该段 heap 虚拟地址并回收 backing。

此接口当前仍是页级接口，还不是更高层的通用对象堆。

## 线程栈模型的切换

这次设计最直接的使用者是 `KTHREAD`。

旧模型中，线程栈创建流程是：

1. 从 PMM 申请 `KE_THREAD_STACK_PAGES` 个连续物理页。
2. 通过 HHDM 把物理基址直接转换为虚拟基址。
3. 把这段 HHDM 线性映射区域当作线程栈使用。

新模型中，线程栈变为：

1. 在 `stack arena` 中申请 `usablePages = 4`、`guardLowerPages = 1` 的 KVA range。
2. 调用 `KeKvaMapOwnedPages`，逐页申请物理页并映射到 usable 区间。
3. 将 `thread->StackBase` 记录为 `UsableBase`，将 `thread->StackGuardBase` 记录为整个 range 的 `BaseAddress`。
4. 调度器始终使用 `StackBase + StackSize` 作为栈顶。
5. 线程终止后，IdleThread reaper 调用 `KeKvaReleaseRange(thread->StackBase)` 完成栈的整体回收。

这个切换的意义在于：

- **线程栈仍然保持虚拟连续**，不会影响 `RSP`、`TSS.RSP0` 和上下文切换逻辑。
- **底层物理页可以离散分配**，不再要求 PMM 提供连续 16KB 物理块。
- **低地址 guard page 自然成立**，向下溢出时更容易暴露异常而不是悄悄踩坏相邻对象。

对应地，`KTHREAD` 结构中也不再保存 `StackPhys`，而改为保存：

- `StackBase`
- `StackSize`
- `StackGuardBase`
- `StackOwnedByKva`

这反映出线程对象现在关心的是“这段栈虚拟地址由谁托管、guard 在哪里”，而不是“底层物理起始页是多少”。

## 启动流程中的初始化顺序

`KE_KVA` 当前被放在一个非常明确的启动顺序中：

1. PMM 初始化完成。
2. imported kernel address space 导入完成。
3. PT HAL 自测完成。
4. `KeKvaInit` 执行。
5. `KeKvaSelfTest` 执行。
6. `RunMemoryObservabilitySelfTest` 执行（`InitKernel` 中的内存可观测性自测）。
7. 之后才继续时间源、时钟事件、线程池与调度器初始化。

这一顺序体现了清晰的依赖关系：

- 没有 PMM，就无法为 KVA-owned range 提供物理 backing。
- 没有 imported root page table，就无法验证 arena hole 或安装新映射。
- 没有 PT HAL，自然也无法安全地把 KVA 作为稳定基础设施暴露给线程与堆。

因此，`KE_KVA` 是介于“基础内存资源层”和“线程/堆等更高层内核对象”之间的中间层，而不是一个独立孤立模块。

## 自测覆盖范围与当前冻结点

`KeKvaSelfTest` 当前覆盖了三类核心行为：

1. **Stack-like range with guard page**
   - 申请一个带低端 guard 的 stack range。
   - 建立映射后确认 guard 页查询结果仍为 unmapped。
   - 确认 usable 首页已经是有效 4KB leaf。
   - 释放后确保整体回收链路可用。

2. **Fixmap**
   - 从 PMM 申请一个物理页。
   - 通过 HHDM 写入测试值。
   - 两次 acquire/release fixmap 槽位，确认临时映射可访问到相同物理内容。

3. **Heap**
   - 申请两页 heap。
   - 在两页上分别写读测试值。
   - 确认页式 heap 的最小增长与释放路径可用。

这些自测共同冻结了当前阶段最重要的结论：

- KVA arena 与 imported boot mappings 不冲突。
- KVA 可以在 imported root 上安装和撤销 4KB 映射。
- 线程栈所需的“guard + usable”布局可以表达出来。
- fixmap 和 page-backed heap 已经具备最小可用语义。

在 `KeKvaSelfTest` 之后，`InitKernel` 还会继续执行 `RunMemoryObservabilitySelfTest`，它会通过真实运行时活动验证 sysinfo 计数是否自洽：

1. 先读取 `KE_SYSINFO_PHYSICAL_MEM_STATS` 与 `KE_SYSINFO_VMM_OVERVIEW` 作为基线。
2. 执行一次 `KePmmAllocPages + KeTempPhysMapAcquire`，检查 PMM 分配统计、`FixmapActiveSlots`、`ActiveKvaRangeCount` 等计数发生预期变化。
3. 回收临时映射与物理页，再执行一次 `KeHeapAllocPages`，检查 heap arena 计数与 PMM 计数联动变化。
4. 全部释放后再次查询，要求关键计数回到基线，确保无泄漏或账本漂移。

## 可观测性与调试语义

当前实现已经内置了几类调试友好的可观测性：

1. `KeKvaInit` 会打印每个 arena 的虚拟地址范围和页数。
2. `KeKvaQueryArenaInfo` 提供总体空闲页和活跃分配数量。
3. `KeKvaQueryUsageInfo` 暴露 `ActiveRangeCount`、`FixmapTotalSlots` 与 `FixmapActiveSlots`，供 `KE_SYSINFO_VMM_OVERVIEW` 汇总。
4. `KeDiagnoseVirtualAddress` 会把 imported-region、PT 映射状态与 KVA 归属信息组合成统一诊断结构。
5. 页故障蓝屏输出新增 `CR2`、`PFERR` 位域，以及 `VMM imported / VMM pt / VMM kva` 三层诊断，能区分 imported 区域、guard page、active fixmap、active heap 或未映射 hole。
6. 线程创建日志会直接输出线程栈 usable base 与 guard base。
7. 线程回收路径在释放 KVA 栈失败时直接触发 panic，避免 silent corruption。

这说明 `KE_KVA` 当前被视为内核关键基础设施，而不是“尽力而为”的辅助模块。

## HHDM 使用策略（当前冻结）

围绕 KVA 与 PMM 的协作，当前实现把 HHDM 使用范围收敛为“启动/诊断优先，运行期 RAM 默认走受控别名或 KVA ownership”：

1. 允许继续使用 HHDM 的角色：
   - boot handoff 与 boot capsule / memory map 访问；
   - ACPI 表发现与固定硬件映射初始化；
   - 早期 bring-up 的栈/TSS handoff；
   - PT HAL 内部与自测；
   - 诊断/自测路径（例如 `KeKvaSelfTest` 中保留的一次 HHDM 交叉校验）。
2. 运行期“短生命周期”物理页访问，优先使用 `KeTempPhysMapAcquire` / `KeTempPhysMapRelease`（基于 fixmap）。
3. 运行期“长生命周期”内存所有权，优先使用 KVA/heap 托管（`KeHeapAllocPages`、KVA range）。
4. MMIO 相关 carve-out 仍按硬件映射语义独立处理，不与普通 RAM ownership 混用；更细策略留待后续变更单独冻结。

## 当前限制与后续工作

当前实现明确依赖如下前提和限制：

1. **单地址空间阶段**
   当前所有 KVA 操作都建立在 imported kernel root page table 之上，没有为未来用户态地址空间切换定义独立的 kernel mapping 管理模型。

2. **仅支持 4KB leaf 映射**
   `KePtMapPage` / `KePtUnmapPage` / `KePtProtectPage` 在这一阶段不负责拆分 2MB / 1GB large leaf，因此 KVA 也自然继承了这一限制。

3. **TLB 维护是本地的**
   页表 HAL 当前只在当前活动 root 上执行局部 `invlpg`，尚未引入跨核 shootdown，也没有多地址空间下的全局失效协议。

4. **尚未引入并发同步**
   KVA 内部的 arena 状态数组和 range record 目前没有锁、IRQL 协议或 per-CPU 机制保护；这一设计默认当前阶段的使用场景仍受较强的执行序假设约束。

5. **Arena 尺寸是静态常量**
   stack / heap / fixmap 的容量目前不是运行期可扩展资源池，而是为第一阶段冻结语义而保留的固定窗口。

6. **Heap 仍然只是页级 foundation**
   `KeHeapAllocPages` 只是“页式虚拟区间 + backing 页”的封装，尚未提供更高层对象分配策略、碎片整理或多种大小类别管理。

后续演进方向可以自然落在以下路径上：

1. 为 KVA 引入同步机制，使其在可抢占和多核环境下具备严格一致性。
2. 把 heap 从页级接口扩展到更完整的 kernel heap / slab / object allocator。
3. 将 fixmap 槽位管理与更多临时映射场景结合，例如页表编辑、I/O 窗口和调试别名。
4. 为线程栈增加更强的调试与保护机制，例如更严格的 overflow 诊断策略。
5. 在未来用户态地址空间模型中，明确 KVA 作为“全局内核映射域”应如何与进程页表协同。

就当前阶段而言，`KE_KVA` 已经成功完成了一个重要冻结点：在不推翻现有 PMM 和 imported PT HAL 的情况下，为内核建立了一个可解释、可验证、可扩展的虚拟地址资源分层，并把线程栈、fixmap 与 page-backed heap 纳入到了同一套生命周期语义之下。
