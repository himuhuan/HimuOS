# HimuOS 项目交接报告

生成时间：2026-04-05

---

## 1. 项目定位与愿景回顾

HimuOS 是一个从零开始设计的 **UEFI x86_64 宏内核操作系统**，面向教学目的。其设计借鉴 Windows NT 的分层宏内核思想，将内核划分为 **Ke（机制层）** 与 **Ex（策略层）** 两个层次，两层之间遵循严格的单向依赖原则。

按照 `Readme.md` 的完整愿景，系统最终应具备：

- 基于四级页表的虚拟内存管理
- Ring 0 / Ring 3 特权级隔离
- 系统调用作为用户态获取内核服务的唯一入口
- 基于优先级和时间片轮转（RR）的多任务调度
- Ex 层对象管理器与 Capability 句柄模型
- GOP 彩色文本界面与键盘循环缓冲输入

明确不包含的范围：**文件系统**、**SMP 多核支持**。

---

## 2. 已完成功能

### 2.1 启动子系统

| 功能 | 说明 |
|------|------|
| UEFI 引导加载器（HimuBootManager） | 自定义 UEFI Loader，完成 GOP 初始化、ELF 内核加载、Boot Capsule 构建、`ExitBootServices` 后跳转内核 |
| Boot Mapping Manifest | 启动期映射目录：HHDM、ACPI 表、Framebuffer/LAPIC/HPET MMIO |
| NX 与空指针保护 | CPU NX 支持检测与启用；`0x0-0xFFF` 空指针保护页 |

### 2.2 物理内存管理（PMM）

| 功能 | 说明 |
|------|------|
| PMM 核心 | 基于 bitmap 的物理页帧分配/释放，支持对齐约束 |
| 启动初始化 | 从 EFI 内存映射自动构建 bitmap |
| 统计追踪 | 全局物理内存使用跟踪（total / free / allocated / reserved） |

### 2.3 虚拟内存管理

| 功能 | 说明 |
|------|------|
| 内核地址空间导入 | 从启动期页表导入持久化内核虚拟地址根 |
| KVA（内核虚拟地址分配） | Stack / Fixmap / Heap 三类 arena 管理 |
| 临时物理映射（fixmap） | 单页临时映射 acquire/release 接口 |
| 页级 Heap Foundation | `KeHeapAllocPages` / `KeHeapFreePages` 页级堆分配 |
| 动态内存分配 | `kmalloc` / `kzalloc` / `kfree`，支持 16/32/64/128/256/512/1024 字节 size class + 大对象专用区间 |
| 固定大小对象池 | `KE_POOL` 用于 KTHREAD 等内核对象的高效分配 |
| 页表操作 | `KePtMapPage` / `KePtUnmapPage` / `KePtProtectPage` / `KePtQueryPage` |
| 进程私有地址空间 | process-private root 创建/销毁、高半区内核映射派生、`KeSwitchAddressSpace()` |

### 2.4 中断与架构支持

| 功能 | 说明 |
|------|------|
| IDT / TSS / GDT | 完整的 x86_64 中断描述符表与任务状态段 |
| 中断入口存根 | 汇编级异常帧保存与分发 |
| 上下文切换 | callee-saved 寄存器上下文切换（`KiSwitchContext`） |
| IRQL 管理 | 中断请求级别状态管理与保护 |
| 临界区 | 自旋锁级临界区原语 |

### 2.5 时间子系统

| 功能 | 说明 |
|------|------|
| 时间源（time source） | 设备-汇抽象，支持 TSC / HPET / PM Timer 后端，纳秒精度 |
| 时钟事件（clock event） | LAPIC Timer 驱动的 deadline 可编程中断 |
| 无 tick 调度驱动 | 按实际 deadline 编程，无固定周期 tick |

### 2.6 线程与调度

| 功能 | 说明 |
|------|------|
| KTHREAD | 线程对象池化管理，支持 joinable / detached 生命周期 |
| 调度器 | 单优先级时间片轮转（RR），10ms 量子，抢占式，tickless |
| 空闲线程 | idle thread 负责已终止线程回收 |
| 单对象等待 | `KeWaitForSingleObject` 统一等待完成路径 |
| KEVENT | 自动/手动复位事件对象 |
| KSEMAPHORE | 计数信号量（count + limit） |
| KMUTEX | 带所有权追踪的互斥量 |
| 线程终止协作 | join / detach / reap 协作语义 |
| 调度期地址空间切换 | `KiSchedule()` 在上下文切换前解析并安装目标 root |

### 2.7 控制台与诊断

| 功能 | 说明 |
|------|------|
| 图形控制台 | 基于 GOP 帧缓冲的彩色文本输出 |
| 串口控制台 | COM1 串口输出（`-serial stdio`） |
| 复用 sink | debug 构建下图形与串口同时输出 |
| kprintf | 格式化输出，支持 `%-` 左对齐 |
| BSOD 诊断 | 蓝屏输出 + `find_bsod_pos.py` 地址定位 |
| 页故障诊断 | `KeDiagnoseVirtualAddress()` 多层诊断（PT / KVA / HHDM / imported region） |
| 系统信息查询 | `KeQuerySystemInformation` 覆盖 CPU / 内存 / 调度器 / 时间 / 页表 / VMM |

### 2.8 Bootstrap-Only 最小用户态

| 功能 | 说明 |
|------|------|
| Ring 3 进入 | 汇编 `SYSRETQ` gadget 首次进入用户态 |
| P1 timer round-trip | 来自 CPL3 的定时器中断往返验证 |
| P2 raw syscall | `int 0x80` 同步入口，`SYS_RAW_WRITE` / `SYS_RAW_EXIT` |
| P3 exit/reap 合同 | teardown-before-termination → idle/reaper reclaim 证据链 |
| Ex bootstrap facade | `ExProcess` 持有 private root + staging，`ExThread` 包装 KTHREAD |
| 回调桥接 | Ke 通过注册式回调与 Ex 交互，无硬编码依赖 |
| Ex 最小对象核心 | `EX_OBJECT_HEADER` 提供 type + refcount；`ExProcess` / `ExThread` / stdout service / waitable object 已具备 bootstrap-scoped 对象身份 |
| 进程私有 handle table | generation-checked private handle、self/stdout/wait handle、`close` / `close-all` 生命周期 |
| Bootstrap capability pilot | 版本化 capability seed block、`SYS_WRITE` / `SYS_CLOSE` / `SYS_WAIT_ONE`，由 `user_caps` profile 回归验证 |

### 2.9 教学与回归基础设施

| 功能 | 说明 |
|------|------|
| 回归 profile 体系 | 12 个稳定 profile（schedule / user_hello / guard_wait / owned_exit / irql_* / pf_*） |
| Demo 框架 | 10 个功能/错误路径/集成演示 |
| OpenSpec 工作流 | spec-driven 变更管理，33 个已归档 change，37 个活跃 spec |
| 构建系统 | Makefile + `bear` 生成 `compile_commands.json`（clangd 支持） |
| 运行与日志采集 | `qemu_capture.sh` 自动化运行与串口日志捕获 |

---

## 3. 已有地基但尚未完成的功能

这些内容在代码中已有明确地基，甚至已经打通 bootstrap-only pilot，但还不能算作 README 愿景下的完整功能：

| 项目 | 现状 | 差距 |
|------|------|------|
| 正式用户态子系统 | 已有 `user_hello` / `user_caps` 两条 bootstrap-only 单进程路径，private root 与 capability seed 已打通 | 无通用用户程序装载、可恢复用户 fault、动态 per-process layout allocator、多进程/多对象用户模型 |
| Ex / Object Manager | 已有最小对象核心：typed/refcounted `EX_OBJECT_HEADER`、`ExProcess` / `ExThread` / stdout service / waitable object | 仍无通用对象管理器、对象命名/目录、类型注册、跨子系统统一对象化 |
| 键盘输入 | 仅 bootloader 级 UEFI `ReadKeyStroke` | 无内核键盘驱动、扫描码翻译、输入缓冲队列 |
| Capability / handle 语义 | 已有 bootstrap-only 的 per-process private handle table、generation-checked self/stdout/wait handle，以及 `SYS_WRITE` / `SYS_CLOSE` / `SYS_WAIT_ONE` pilot | 仍不是 README 承诺的通用 Ex capability handle 模型，缺少 open/dup、更多对象类型与正式 ABI |

---

## 4. 待完成功能清单

以下功能是 `Readme.md` 愿景明确承诺、但当前尚未完整实现的。需要特别注意的是：2026-04-05 合入主线的 PR `#39` / `#40` / `#41` 已经把 Ex 对象、私有 handle table 与 capability syscall 的 bootstrap-only 前置工作带进 `main`，因此这些条目不应再被描述为“完全未开始”，而应理解为“已有 pilot / groundwork，但离正式功能仍有明显差距”。

### 4.1 优先级调度（Priority Scheduling）

- **愿景**：README 承诺"基于优先级和时间片轮转（RR）的调度器"
- **现状**：当前调度器为**单优先级 RR**；`KTHREAD.Priority` 仅为保留字段
- **缺口**：优先级队列、优先级判定逻辑、（可选的）优先级反转保护
- **估计复杂度**：低-中。调度器基础设施成熟，主要是将单 FIFO ready queue 改为多级 priority queue

### 4.2 键盘循环缓冲输入（Keyboard Input）

- **愿景**：README 承诺"键盘循环缓冲输入"与"标准 QWERTY 键盘输入支持"
- **现状**：无内核键盘驱动。仅 bootloader 有 UEFI `ReadKeyStroke`
- **缺口**：PS/2 or USB HID 键盘驱动 → IRQ1 中断处理 → 扫描码翻译 → 循环缓冲队列 → 内核读取 API
- **估计复杂度**：中。需要 IDT 注册 IRQ1、编写扫描码表、实现环形缓冲

### 4.3 Ex 层 / Object Manager

- **愿景**：README 描述 Ex 层引入对象管理器，将内核资源统一抽象为受控对象
- **现状**：PR `#39` 已把 Ex 从“纯 facade”推进到 bootstrap-scoped 最小对象核心：`EX_OBJECT_HEADER`（type + refcount）、`ExProcess` / `ExThread`、`EX_STDOUT_SERVICE` / `EX_WAITABLE_OBJECT` 已具备对象身份与 retain/release 路径
- **已完成部分**：bootstrap launch/init/teardown owner、runtime alias publish/unpublish、最小 typed object lifetime
- **缺口**：仍无通用 Object Manager、对象命名/目录、类型注册、跨子系统统一对象化
- **完成度判断**：已起步，但距离 README 承诺的正式 Object Manager 仍有明显差距
- **估计复杂度**：高。这是架构层变更，需要设计对象模型后逐步迁移现有内核资源

### 4.4 Capability 句柄模型

- **愿景**：README 把它作为用户态/内核态隔离的核心安全机制
- **现状**：PR `#40` / `#41` 已落地 bootstrap-only per-process private handle table：generation-checked handle、rights bits、process/thread self + stdout + wait handle，以及 resolve/close/close-all 生命周期
- **已完成部分**：per-process table、capability rights、seed block 向用户态分发初始 handles、stale-handle rejection after close
- **缺口**：仍缺通用对象句柄空间、open/dup、更多对象类型、跨进程/跨子系统语义，以及 README 级 capability 安全模型
- **完成度判断**：已起步（bootstrap capability pilot 已合入主线）
- **估计复杂度**：中-高。基础骨架已有，但一般化仍依赖 Object Manager 持续扩展
- **依赖**：4.3 Ex 层 / Object Manager

### 4.5 Handle-Oriented 正式 System Call 接口

- **愿景**：README 承诺"系统调用是用户态获取内核服务的唯一入口"
- **现状**：PR `#41` 已在共享 `int 0x80` 入口下新增 bootstrap capability syscall pilot：`SYS_WRITE` / `SYS_CLOSE` / `SYS_WAIT_ONE`，handle lookup 与 rights check 由 Ex 负责；`SYS_RAW_*` 仍保留
- **已完成部分**：独立 capability syscall number range、版本化 capability seed block、stdout write / close / wait-one 基础调用
- **缺口**：仍缺正式 syscall 分发表、稳定 ABI/错误码 contract、通用 handle 参数校验、非 bootstrap 服务扩展
- **完成度判断**：已有前置 pilot，但 README 承诺的正式 syscall 表面仍未完成
- **估计复杂度**：中-高。入口雏形已有，但正式化仍需要 handle/object 模型继续扩展
- **依赖**：4.4 Capability 句柄模型

### 4.6 完整用户态子系统 / 通用用户程序模型

- **愿景**：README 承诺为用户程序提供隔离地址空间
- **现状**：已有 `user_hello` 与 `user_caps` 两条 bootstrap-only 证据链，支持 process-private root、最小 capability seed / stdout / wait handle pilot，但仍是静态 staging layout 的单进程 bring-up
- **缺口**：通用 ELF 装载器、动态 per-process 布局分配、用户 fault 可恢复路径、多进程/多对象用户模型
- **完成度判断**：bootstrap bring-up 增强了，但距离“完整用户态子系统”仍然很远
- **估计复杂度**：高。这是最终用户态的集大成目标
- **依赖**：4.3、4.4、4.5 均需进一步一般化

---

## 5. 架构决议与已规划路线

项目已在 `docs/draft/userspace.md` 中确立了分阶段演进路线。与 2026-04-04 版本相比，2026-04-05 合入的 PR `#39` / `#40` / `#41` 已把 Phase C 与 Phase D 的 bootstrap-only 前置工作带入 `main`，因此这里不再把它们简单标为“未开始”。

| 阶段 | 目标 | 状态 |
|------|------|------|
| **Phase A** | 建立极薄 Ex 层，收口 bootstrap 语义 | **已完成** — Ex facade 持有 launch/init/teardown owner，回调桥接就位 |
| **Phase B** | 落地独立用户地址空间机制 | **已完成（最小范围）** — process-private root、dispatch-time root switch、live layout 校验 |
| Phase C | Handle Table / Capability 模型 | **已起步（bootstrap-only）** — private handle table、generation-checked self/stdout/wait handle、`close` / `close-all` 与 seed block 已落地 |
| Phase D | 正式 Syscall 表面 | **前置 pilot 已落地** — 共享 `int 0x80` 入口下已有 `SYS_WRITE` / `SYS_CLOSE` / `SYS_WAIT_ONE`，但仍属 bootstrap ABI，不是正式 syscall 接口 |
| Phase E | ELF Loader / 通用用户程序 | 未开始 |

---

## 项目健康度总评

| 维度 | 评价 |
|------|------|
| **架构清晰度** | 优秀 — Ke/Ex 分层、设备-汇模式、回调桥接，模块边界清楚 |
| **代码质量** | 良好 — Microsoft 代码风格统一，诊断覆盖充分 |
| **可观测性** | 优秀 — sysinfo 查询、串口日志、BSOD 诊断、页故障多层诊断 |
| **回归保护** | 优秀 — 12 个稳定 profile、自动化日志采集、`qemu_capture.sh` 流程 |
| **文档与规范** | 良好 — OpenSpec spec-driven 工作流，37 个 spec、33 个已归档 change |
| **教学友好度** | 优秀 — demo 框架、分阶段验证、clean-pass / error-path / diagnostics 三类 profile |
| **技术债** | 低 — Phase A/B 的 Ex 收口已经完成，Ke 层干净 |

### 一句话总结

> HimuOS 已经完成了一个**架构成熟、机制完整的 Ke 层教学内核**——从 UEFI 启动到虚拟内存、线程调度、同步原语、诊断观测一应俱全——并且在 bootstrap-only 用户态上继续向前推进：截至 PR `#39` / `#40` / `#41`，Ex 最小对象核心、进程私有 handle table 与 capability syscall pilot 已经进入 `main`。这意味着 Object/Capability/Syscall 不再是“从零开始”，但它们仍只覆盖 bootstrap 切片；接下来仍应先补齐优先级调度与键盘输入这两个 README 明确缺口，再把当前 pilot 从 bootstrap 专用语义推进为通用用户态 contract。
