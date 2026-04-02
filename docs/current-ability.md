# HimuOS 当前能力盘点

更新时间：2026-04-02

## 盘点口径

这份文档只回答一件事：**某项功能现在“有没有”**，不评价其成熟度、性能、健壮性或文档完善度。

本次盘点综合了三类信息源：

- `Readme.md`：对外承诺与教学目标
- `agents.md`：当前 agent 工作约束与默认执行约定
- 当前源码与 OpenSpec：实际已落地的模块、入口和规格

判定规则如下：

- 代码中存在明确模块、公开 API、启动入口或 demo/profile，可判定为“已有”
- 只有少量地基、占位字段或局部痕迹，但还不能形成完整功能，不算“已有”
- `Readme.md` 已明确声明**不包含**的内容，不记入缺口

另外，按你的要求，这里**不把编译/运行流程上的小问题当成功能缺失**；并默认现有运行与验证链路已经过多轮 review，可视为这些能力成立。

## 当前已支持的功能

整体上，HimuOS 当前已经是一个**以 Ke 层为主的教学原型系统**：启动、内存、时间、控制台、中断、调度、同步、诊断、回归 profile 都已经具备明确实现。

| 能力域 | 结论 | 主要依据 |
| --- | --- | --- |
| UEFI 启动与内核装载 | 已有 | `src/boot/v2/efi_main.c`、`src/boot/v2/bootloader.c`、`src/boot/v2/blmm/*` 已实现 UEFI Loader、ELF 加载、Boot Capsule 构建、`ExitBootServices` 后跳转内核 |
| Boot handoff / Boot Mapping Manifest | 已有 | `src/include/boot/boot_capsule.h`、`src/include/boot/boot_mapping_manifest.h`、`src/boot/v2/blmm/*`、OpenSpec `boot-mapping-manifest` |
| HHDM / ACPI / MMIO 启动期映射 | 已有 | `src/boot/v2/blmm/hhdm.c`、`src/boot/v2/blmm/acpi.c`、`src/boot/v2/blmm/capsule.c` 已覆盖 HHDM、RSDP/ACPI 表、Framebuffer/LAPIC/HPET MMIO |
| NX 与空指针保护地基 | 已有 | `src/boot/v2/bootloader.c` 中 `CpuSupportsNx` / `EnableNxe` / `BootClaimNullGuardPage`，OpenSpec `boot-null-detection` |
| GOP 图形输出 | 已有 | `src/drivers/video/video_driver.c`、`src/drivers/video/efi/video_efi.c`，内核控制台基于 GOP 帧缓冲输出 |
| 内核控制台与串口日志 | 已有 | `src/kernel/ke/console/console.c`、`src/kernel/ke/console/sinks/*`；debug 构建下图形与串口复用 sink |
| 中断、异常、IDT、TSS、上下文切换 | 已有 | `src/arch/amd64/idt.c`、`src/arch/amd64/intr_stub.asm`、`src/arch/amd64/context_switch.asm`、`src/arch/amd64/pm.c` |
| 时间源子系统 | 已有 | `src/kernel/ke/time/time_source.c`、`src/kernel/ke/time/sinks/*`、`src/drivers/time/tsc_driver.c`、`pmtimer_driver.c`、`hpet_driver.c` |
| 时钟事件（clock event） | 已有 | `src/kernel/ke/time/clock_event.c`、`src/kernel/ke/time/sinks/lapic_clockevent_sink.c`、`src/drivers/time/lapic_timer_driver.c` |
| 物理内存管理（PMM） | 已有 | `src/kernel/ke/pmm/pmm_boot_init.c`、`src/kernel/ke/pmm/pmm_device.c`、OpenSpec `pmm-core` / `pmm-boot-init` / `pmm-bitmap-sink` |
| 导入式内核地址空间与页表操作 | 已有 | `src/kernel/ke/mm/address_space.c` 提供 `KeImportKernelAddressSpace`、`KePtQueryPage`、`KePtMapPage`、`KePtUnmapPage`、`KePtProtectPage` |
| KVA（内核虚拟地址分配） | 已有 | `src/kernel/ke/mm/kva.c` 已实现 stack/fixmap/heap 三类 arena，OpenSpec `kernel-virtual-allocator` / `kernel-fixmap` |
| 临时物理页映射（fixmap / temp phys map） | 已有 | `src/kernel/ke/mm/kva.c` 中 `KeFixmapAcquire/Release`、`KeTempPhysMapAcquire/Release`，OpenSpec `temp-phys-map-safety` |
| 页级 heap foundation | 已有 | `src/kernel/ke/mm/kva.c` 中 `KeHeapAllocPages` / `KeHeapFreePages`，OpenSpec `kernel-heap-foundation` |
| 动态内存分配 | 已有 | `src/kernel/ke/mm/allocator.c` 中 `kmalloc` / `kzalloc` / `kfree`，并可做地址诊断与统计 |
| 固定大小对象池 | 已有 | `src/kernel/ke/mm/pool.c`、`src/include/kernel/ke/pool.h`，OpenSpec `kernel-object-pool` |
| 系统信息查询（sysinfo） | 已有 | `src/kernel/ke/sysinfo/sysinfo.c`、`src/include/kernel/ke/sysinfo.h`，已支持 CPU、页表、内存、GDT/TSS/IDT、时间、scheduler、VMM 等查询 |
| 内核线程（KTHREAD） | 已有 | `src/kernel/ke/thread/kthread.c`、`src/include/kernel/ke/kthread.h`，支持创建 joinable / detached 线程 |
| 调度器 | 已有 | `src/kernel/ke/thread/scheduler/scheduler.c`、`timer.c`、`diag.c`，OpenSpec `scheduler` / `scheduler-observability` |
| 单对象等待模型 | 已有 | `src/kernel/ke/thread/scheduler/wait.c`，提供 `KeWaitForSingleObject` 与统一 wait-completion 路径，OpenSpec `dispatcher-objects` |
| 事件（KEVENT） | 已有 | `src/kernel/ke/thread/scheduler/sync.c`、`src/include/kernel/ke/event.h`、OpenSpec `kevent` |
| 信号量（KSEMAPHORE） | 已有 | `src/kernel/ke/thread/scheduler/sync.c`、`src/include/kernel/ke/semaphore.h`、OpenSpec `ksemaphore` |
| 互斥量（KMUTEX） | 已有 | `src/kernel/ke/thread/scheduler/sync.c`、`src/include/kernel/ke/mutex.h`、OpenSpec `kmutex` |
| IRQL 与临界区保护 | 已有 | `src/kernel/ke/irql.c`、`src/kernel/ke/critical_section.c`，OpenSpec `critical-section` / `irql-protection` |
| 线程终止协作与 join/detach 语义 | 已有 | `src/kernel/ke/thread/scheduler/scheduler.c`、`src/kernel/demo/thread.c` |
| 诊断与 BSOD 辅助 | 已有 | `src/kernel/hodbg.c`、`scripts/find_bsod_pos.py`、OpenSpec `ke-diagnosis-interfaces` / `page-fault-diagnostic-context` / `page-fault-diagnostics-hardening` |
| 教学 demo / regression profile | 已有 | `src/kernel/demo/*`、`makefile`、OpenSpec `ke-regression-profiles` |

### 当前已有功能的边界

如果用一句话概括当前系统状态，可以认为：

> **HimuOS 当前已经完成了一个相当完整的 Ke 层教学原型。**

它已经不只是“能启动的内核骨架”，而是具备以下连续能力链：

1. UEFI Loader 把内核与启动环境交给内核
2. 内核完成 PMM、页表导入、KVA、heap foundation、allocator、pool 初始化
3. 内核带起时间源、clock event、scheduler
4. 调度器能驱动 KTHREAD、wait、event、semaphore、mutex
5. 系统具备 sysinfo / 诊断 / regression profile 这些教学友好的观测入口

## 有地基，但还不能算“该功能已经有”

下面这些项**不能**因为代码里出现了一点痕迹，就算成功能已经存在：

| 项目 | 现状 | 为什么还不能算“已有” |
| --- | --- | --- |
| Ring 3 GDT 段 | 有痕迹 | `src/arch/amd64/pm.c` 中已经建了 user code/data segment，但没有真正的用户态线程、用户态地址空间切换或返回 Ring 3 的执行路径 |
| 键盘输入 | 只有 bootloader 级输入 | `src/boot/v2/io.c` 能通过 UEFI `ReadKeyStroke` 读取控制台输入，但这只是 bootloader 输入，不是 README 承诺的“内核键盘循环缓冲输入” |
| handle 语义 | 只在局部存在 | KVA/fixmap 使用了 opaque handle/token，但这不是 README 所说的 Ex 层 capability handle / object handle 模型 |

## 在教学原型约束下，仍需完成的功能

下面这些功能是结合 `Readme.md` 与 `agents.md` 后，**仍应视为待补齐项**。这里仍然只判断“有没有”，不讨论做得是否精细。

| 待完成功能 | 为什么仍算缺口 | 现有状态 |
| --- | --- | --- |
| 用户态执行路径（Ring 3 真正落地） | README 明确承诺“内核态与用户态安全隔离” | 目前只有 Ring 3 段描述符地基，没有用户态线程/进程运行入口 |
| 用户态地址空间 / 进程级地址空间切换 | README 把虚拟内存目标描述为“为内核和用户程序提供隔离地址空间” | 当前内存管理几乎全部围绕 imported kernel root 与 KVA，没有进程地址空间模型 |
| 系统调用入口 | README 明确承诺“系统调用是用户态获取内核服务的唯一入口” | 代码中没有 `syscall/sysret`、`int 0x80`、LSTAR 等系统调用通路实现 |
| Ex 层 / Object Manager | README 明确写了 Ke/Ex 两层结构，Ex 层负责对象管理器 | 当前仓库基本只有 Ke 层；没有独立的 Ex 层目录、对象管理器或对象命名/生命周期框架 |
| Capability 句柄模型 | README 把它作为用户态/内核态隔离的重要机制 | 当前没有对象句柄表、capability 校验、句柄引用/销毁等子系统 |
| 优先级调度 | README 承诺“基于优先级和时间片轮转（RR）的调度器” | 当前 OpenSpec `scheduler` 与代码都明确是**单优先级 RR**；`KTHREAD.Priority` 只是保留字段 |
| 键盘循环缓冲输入 | README 明确承诺“键盘循环缓冲输入”与“标准 QWERTY 键盘输入支持” | 当前没有内核键盘驱动、扫描码处理或输入缓冲队列；只有 bootloader 的 UEFI 文本输入 |

## 不应计入缺口的项

这些内容在当前阶段**不应**被列为“还没做完”，因为仓库本来就没有承诺此阶段提供：

- 文件系统：`Readme.md` 已明确写明“不包含文件系统”
- SMP / 多核支持：`Readme.md` 已明确写明“不包含多核（SMP）支持”

## 结论

当前的 HimuOS 更准确的定位是：

- **已经完成的部分**：UEFI 启动、Ke 层核心机制、内核内存管理、线程调度与同步、诊断与教学回归 profile
- **尚未完成的部分**：README 中更接近“完整 OS 对外语义”的那一层，尤其是用户态、系统调用、Ex/Object Manager、capability handle、键盘输入、优先级调度

换句话说，当前系统已经是一个**功能明确、结构清晰的内核教学原型**，但还**不是** README 最初叙述里的那个“具备用户态/系统调用/Ex 层对象模型”的更完整版本。
