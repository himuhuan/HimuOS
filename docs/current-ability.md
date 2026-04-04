# HimuOS 当前能力盘点

更新时间：2026-04-04

## 盘点口径

这份文档只回答一件事：**某项功能现在“有没有”**，不评价其成熟度、性能、健壮性或文档完善度。

本次盘点综合了四类信息源：

- `Readme.md`：对外承诺、回归 profile 说明与阶段边界
- 当前工作树与最近提交：尤其是 `origin/main` 之后围绕 P3 exit/reap 与 `complete-ex-bootstrap-migration` 的本地提交
- 已合并 PR：近期重点参考 `#29`～`#33`，其中与最小用户态切片直接相关的是 `#31`、`#32`、`#33`
- 当前源码与 OpenSpec：归档 change 用来确认既有合同，活跃 change 用来核对边界、非目标与仍在推进的表述

判定规则如下：

- 代码中存在明确模块、公开 API、启动入口或 demo/profile，可判定为“已有”
- 已合并 PR 或当前 HEAD 已落地的日志锚点、回归合同和注释语义，可作为“已有”的补强证据
- 只有少量地基、占位字段或局部痕迹，但还不能形成完整功能，不算“已有”
- 活跃 OpenSpec change 的 proposal/design/tasks 主要用于说明能力边界与演进方向；如果 checklist 尚未归档，但当前 HEAD 已有实现，则能力判断仍以当前代码与提交事实为准
- `Readme.md` 已明确声明**不包含**的内容，不记入缺口

另外，按你的要求，这里**不把编译/运行流程上的小问题当成功能缺失**；并默认现有运行与验证链路已经过多轮 review，可视为这些能力成立。

## 本次对齐重点

- 已合并 PR `#31` 把 `user_hello` 固定成 bootstrap-only 的最小 Ring 3 bring-up 切片
- 已合并 PR `#32` 把 P1 first-entry / timer round-trip 证据正式收敛到同一个 `user_hello` profile
- 已合并 PR `#33` 把 P2 rejected raw write probe / successful hello write / `SYS_RAW_EXIT` 证据链固定下来
- 当前 HEAD 的 `complete-ex-bootstrap-migration` 已把 bootstrap launch / init / teardown 所有权收口到 Ex：`user_hello` 通过 Ex facade 启动，`InitKernel()` 通过 `ExBootstrapInit()` 统一装配 bootstrap runtime
- bootstrap ABI 常量、固定窗口约束说明和关键日志锚点现已由 `src/include/kernel/ex/ex_bootstrap_abi.h` 对外暴露；scheduler / timer / finalizer / reaper 通过 Ex 注册的 ownership query 与 callbacks 判断 bootstrap 路径，`KTHREAD_FLAG_BOOTSTRAP_USER` 与 `UserBootstrapContext` 已删除
- 正式进程地址空间 / 独立 CR3 语义仍未开始落地；这部分从 `complete-ex-bootstrap-migration` 的 Phase B 才开始承接

## 当前已支持的功能

整体上，HimuOS 当前已经是一个**仍以 Ke 层为主、但 bootstrap launch / init / teardown 已收口到 Ex 的教学原型系统**：启动、内存、时间、控制台、中断、调度、同步、诊断、回归 profile 都已经具备明确实现；另外，独立 `user_hello` profile 已打通最小 Ring 3 bring-up、P1 timer round-trip、P2 raw syscall 自检，并在当前 HEAD 上进一步把 P3 的 teardown-before-termination -> idle/reaper reclaim 证据链固定成显式合同，但这还不能等同于完整用户态子系统。

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
| bootstrap-only 最小用户态执行路径 | 已有 | `src/kernel/demo/user_hello.c`、`src/kernel/ex/ex_bootstrap.c`、`src/kernel/ex/ex_bootstrap_adapter.c`、`src/kernel/ke/user_bootstrap.c`、`src/kernel/ke/user_bootstrap_syscall.c`、`src/arch/amd64/user_bootstrap.asm` 已打通独立 `user_hello` profile 下的 staging 用户映射、首次进入 Ring 3、来自 CPL3 的 P1 timer round-trip、P2 raw syscall 证据链，并用 `bootstrap teardown complete` / `fallback staging reclaim` 锚点明确 P3 teardown-before-termination → idle/reaper reclaim 合同；当前 bootstrap launch / init / teardown owner 已收口到 Ex，`user_hello` 经由 Ex facade 启动，scheduler / timer / finalizer / reaper 通过 Ex ownership query / callbacks 判断 bootstrap 路径，`KTHREAD_FLAG_BOOTSTRAP_USER` 与 `UserBootstrapContext` 已删除 |
| 最小 raw syscall 入口 | 已有 | `src/include/kernel/ex/ex_bootstrap.h`、`src/include/kernel/ex/ex_bootstrap_abi.h`、`src/kernel/ex/ex_bootstrap.c`、`src/kernel/ke/user_bootstrap_syscall.c`、`src/kernel/init/init.c` 已实现同步 `int 0x80` 入口、`SYS_RAW_WRITE` / `SYS_RAW_EXIT`、bootstrap user-range 校验与 bounded copy-in helper，以及 P2/P3 稳定日志锚点；bootstrap ABI 常量、固定窗口语义和外部可见证据锚点现由 Ex 头文件暴露，而 trap / copy-in 机制仍由 Ke 提供 |
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

> **HimuOS 当前已经完成了一个以 Ke 层为主、带有 bootstrap-only 最小用户态 bring-up 切片的教学原型。**

这里的用户态能力只指独立 `user_hello` profile 下的最小执行路径与 raw syscall 入口，不应外推为完整进程地址空间、正式系统调用子系统或 Ex 层对象模型。

更具体地说，当前 `user_hello` 只是一条 bootstrap/staging 性质的脚手架测试路径，用来验证“最小用户页映射 -> 首次进入 Ring 3 -> 来自 CPL3 的 P1 timer round-trip -> P1 gate armed -> invalid raw write rejected -> hello write succeeds -> `SYS_RAW_EXIT` -> bootstrap teardown complete -> thread terminated -> idle/reaper reclaimed -> back to idle”这条最小证据链。当前 launch / init / teardown 与对外 bootstrap ABI surface 已由 Ex facade 持有，Ke 继续负责 staging、trap、线程与调度等底层机制；当前代码也已经删除 `KTHREAD_FLAG_BOOTSTRAP_USER` 与 `UserBootstrapContext`，因此 scheduler / timer / finalizer / idle/reaper 只通过 Ex 注册的 ownership query / callback contract 判断是否走 bootstrap 路径。这里的 P1 gate、后续 P2 raw syscall 自检以及当前 HEAD 上进一步补强的 P3 exit/reap 合同，都只是同一个 `user_hello` profile 内部的分阶段验证，不是新增独立的 P1-only、P2-only 或 P3-only profile。现阶段共享 imported root 仍保留 boot 阶段遗留的低 2GB identity mapping，因此 bootstrap 固定用户窗口需要显式避开该区域；这属于当前 bring-up 模型的临时约束，不代表长期用户虚拟地址布局合同，也不意味着当前 bootstrap raw syscall 已经承诺未来正式用户态 ABI。P3 现在明确要求正常路径由 `SYS_RAW_EXIT` 在进入 `KeThreadExit()` 前完成 bootstrap 用户资源释放并打印 `bootstrap teardown complete`，scheduler finalizer 只保留 `fallback staging reclaim` 这类防御性兜底语义。而 `complete-ex-bootstrap-migration` 的 Phase B 才开始承接正式地址空间语义：届时系统预期会为用户态重建或派生独立页表根，并以干净的 low-half 布局替代当前 fixed bootstrap window，因此这条 staging 限制与长期目标并不冲突。

它已经不只是“能启动的内核骨架”，而是具备以下连续能力链：

1. UEFI Loader 把内核与启动环境交给内核
2. 内核完成 PMM、页表导入、KVA、heap foundation、allocator、pool 初始化
3. 内核带起时间源、clock event、scheduler
4. 调度器能驱动 KTHREAD、wait、event、semaphore、mutex
5. 系统具备 sysinfo / 诊断 / regression profile 这些教学友好的观测入口
6. 独立 `user_hello` profile 能把受控 payload 送入 Ring 3，先证明来自 CPL3 的 P1 timer round-trip，再在同一个 profile 内通过 rejected raw write probe、successful hello write 和 `SYS_RAW_EXIT` 完成最小 raw syscall 证据链，并继续以 teardown-complete -> thread terminated -> idle/reaper reclaimed 证明 P3 exit/reap 合同

## 有地基，但还不能算“该功能已经有”

下面这些项**不能**因为代码里出现了一点痕迹，就算成功能已经存在：

| 项目 | 现状 | 为什么还不能算“已有” |
| --- | --- | --- |
| 正式用户态子系统 / 进程模型 | 只有 bootstrap bring-up | 当前已有独立 `user_hello` profile、staging 用户映射、Ring 3 入口和 raw syscall 闭环，但仍复用 imported root，不具备正式进程对象、独立进程地址空间或可恢复的用户 fault 模型 |
| 键盘输入 | 只有 bootloader 级输入 | `src/boot/v2/io.c` 能通过 UEFI `ReadKeyStroke` 读取控制台输入，但这只是 bootloader 输入，不是 README 承诺的“内核键盘循环缓冲输入” |
| handle 语义 | 只在局部存在 | KVA/fixmap 使用了 opaque handle/token，但这不是 README 所说的 Ex 层 capability handle / object handle 模型 |

## 在教学原型约束下，仍需完成的功能

下面这些功能是结合 `Readme.md`、当前提交、已合并 PR 与 OpenSpec 边界后，**仍应视为待补齐项**。这里仍然只判断“有没有”，不讨论做得是否精细。

| 待完成功能 | 为什么仍算缺口 | 现有状态 |
| --- | --- | --- |
| 完整用户态子系统 / 通用用户程序模型 | README 明确承诺“内核态与用户态安全隔离” | 当前只有挂在独立 `user_hello` profile 下的 bootstrap-only 最小执行路径，尚不具备通用用户程序装载、可恢复故障或稳定用户对象模型 |
| 正式进程地址空间 / 进程级地址空间切换 | README 把虚拟内存目标描述为“为内核和用户程序提供隔离地址空间” | 这部分从 `complete-ex-bootstrap-migration` 的 Phase B 才开始承接；当前用户页仍以 staging 方式挂在 imported root 上，不是正式进程地址空间模型 |
| handle-oriented 正式 syscall 表面 | README 明确承诺“系统调用是用户态获取内核服务的唯一入口” | 当前只有同步 `int 0x80` 的 bootstrap raw syscall 入口，且 ABI 仅限 `SYS_RAW_WRITE` / `SYS_RAW_EXIT`，还不是正式的 handle-oriented syscall 接口 |
| Ex 层 / Object Manager | 只有 bootstrap 适配薄壳 | 当前 Ex 已成为 bootstrap launch / init / teardown owner，并对外暴露 bootstrap facade / ABI；但这仍只是 bootstrap-only 薄运行时，不具备正式对象管理器、对象命名/生命周期框架或通用进程模型 |
| Capability 句柄模型 | README 把它作为用户态/内核态隔离的重要机制 | 当前没有对象句柄表、capability 校验、句柄引用/销毁等子系统 |
| 优先级调度 | README 承诺“基于优先级和时间片轮转（RR）的调度器” | 当前 OpenSpec `scheduler` 与代码都明确是**单优先级 RR**；`KTHREAD.Priority` 只是保留字段 |
| 键盘循环缓冲输入 | README 明确承诺“键盘循环缓冲输入”与“标准 QWERTY 键盘输入支持” | 当前没有内核键盘驱动、扫描码处理或输入缓冲队列；只有 bootloader 的 UEFI 文本输入 |

## 不应计入缺口的项

这些内容在当前阶段**不应**被列为“还没做完”，因为仓库本来就没有承诺此阶段提供：

- 文件系统：`Readme.md` 已明确写明“不包含文件系统”
- SMP / 多核支持：`Readme.md` 已明确写明“不包含多核（SMP）支持”

## 结论

当前的 HimuOS 更准确的定位是：

- **已经完成的部分**：UEFI 启动、Ke 层核心机制、内核内存管理、线程调度与同步、诊断与教学回归 profile，以及挂在 `user_hello` 上、由 Ex 持有 launch / init / teardown owner 的 bootstrap-only P1/P2/P3 最小用户态证据链
- **尚未完成的部分**：README 中更接近“完整 OS 对外语义”的那一层，尤其是正式进程地址空间、handle-oriented syscall、Ex/Object Manager、capability handle、键盘输入、优先级调度

换句话说，当前系统已经是一个**功能明确、结构清晰的内核教学原型**，其中还带有一条 bootstrap-only 的最小用户态 bring-up 路径；但它还**不是** README 最初叙述里的那个“具备正式进程地址空间 / handle-oriented syscall / Ex 层对象模型”的更完整版本。
