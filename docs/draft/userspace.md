# HimuOS 架构决议报告：先立薄 Ex 边界，再塑独立地址空间

## 决议概述

经过对当前代码库的依赖关系与分层债务评估，团队就“先做 Ex 层还是先做独立地址空间”达成混合共识：**采纳方案 2 的时序（先 Ex），并严格施加方案 1 的范围约束（极薄化）**。

核心路线定调为：**优先引入极薄的 Ex（执行体）边界，确立进程生命周期与资源归属权；紧接着实现正式的独立用户地址空间机制。** 当前阶段严禁横向铺开完整的对象管理器（Object Manager）、Capability 句柄表或正式系统调用（Syscall）表面。

> 注：下文“架构痛点”“当前现状”等描述保留的是本决议提出时的背景状态。当前 HEAD 已完成 Phase A 中关于 bootstrap launch / init / teardown owner 的收口：`user_hello` 现经 Ex facade 启动，`InitKernel()` 通过 `ExBootstrapInit()` 统一装配 runtime，`KTHREAD_FLAG_BOOTSTRAP_USER` 与 `UserBootstrapContext` 已删除，scheduler / timer / finalizer / reaper 通过 Ex ownership query / callback contract 判断 bootstrap 路径。保留这些旧表述是为了说明为何要先立薄 Ex 边界，不表示这些 Ke 残留今天仍然存在。

---

## 架构痛点与决策依据

在本决议提出时，系统虽然成功跑通了 `user_hello` 的 P1-P3 最小证据链，但为了快速验证，用户态语义已经严重侵入了 Ke（内核）层的核心机制。若直接在此时推进独立地址空间，会导致架构污染进一步加剧。

| 污染位置 | 决议前现状 | 若“先做地址空间”的恶化后果 | 引入薄 Ex 层的解决方案 |
| :--- | :--- | :--- | :--- |
| **KTHREAD 结构体** | 挂载了 `UserBootstrapContext` 指针与 `KTHREAD_FLAG_BOOTSTRAP_USER`。 | 地址空间指针将被迫继续塞入 `KTHREAD`，导致机制与策略彻底混用。 | 将这些字段上移至新创建的 `EPROCESS` / `ETHREAD` 结构中。 |
| **调度器 Trampoline** | 硬编码检查上述指针以决定是否进入 Ring 3。 | 切换 CR3 的逻辑将被硬编码进 Ke 的线程分发器。 | 改为注册式回调，调度器只负责触发，Ex 层决定是否执行用户态转换。 |
| **线程 Finalizer** | 包含 `fallback staging reclaim` 等特定的资源回收硬编码逻辑。 | 页表销毁逻辑将侵入通用的内核线程回收流。 | 回收逻辑封装至 Ex 层的生命周期管理（Teardown）中。 |
| **系统调用分发** | Raw syscall 逻辑直接存放在 `ke/` 目录下。 | 将错就错，导致 Ke 层成为用户态特例的收容所。 | 将相关逻辑平移至 Ex 层或由 Ex 层进行合理封装。 |

**核心结论**：`EPROCESS` 天然是独立地址空间（PML4 根）的归属者（Owner）。没有这个所有者，新的页表机制在 Ke 层无处安放。先立边界，可以避免在错误的层级建造“准进程”容器并反复推倒重来。

---

## 两阶段实施路线图

为了平稳过渡并清偿技术债，开发工作将拆分为两个连续的阶段：

### Phase A：建立极薄 Ex 层（收口当前状态）

此阶段的目标是**重构而非增加新特性**，重点在于将 Ke 层清理干净，维持 `user_hello` 日志锚点和证据链不变。

* **创建薄对象**：实现最基础的 `ExProcess` 和 `ExThread`。`ExProcess` 负责持有当前的 staging 状态和生命周期；`ExThread` 作为包裹 `KTHREAD` 的上层容器。
* **净化 Ke 层**：从 `KTHREAD` 中彻底移除 `UserBootstrapContext` 和 `KTHREAD_FLAG_BOOTSTRAP_USER`。
* **引入回调机制**：在 Scheduler 中替换硬编码的用户态特判，引入 `KiThreadEnterCallback` 与 `KiThreadFinalizeCallback`，由 Ex 层在初始化时注册具体实现。
* **重构测试入口**：修改 `user_hello.c` 演示流，统一通过 `ExCreateProcess()` -> `ExCreateThread()` -> `ExStartThread()` 链条启动。
* **目录规整**：将 `user_bootstrap.c` 与 `user_bootstrap_syscall.c` 移交 Ex 层管理或封装。

注：上述收口已在当前 HEAD 落地，当前文档保留它们作为 Phase A 的决议目标与实施依据。正式进程地址空间 / 独立 CR3 语义仍未进入当前实现，按本决议仍归入 Phase B。

### Phase B：落地独立用户地址空间机制

在 Phase A 确立了 `EPROCESS` 作为合法的资源归属者后，顺理成章地推进内存机制的演进。**正式地址空间语义从这一阶段才开始承接。**

* **升级归属权**：`EPROCESS.AddressSpace` 的语义从“引用共享 imported root”升级为“持有独立的 PML4 根并克隆内核高半区”。
* **Ke 提供纯机制**：Ke 层新增 `KeSwitchAddressSpace(phys_addr)` 接口，仅负责 CR3 寄存器写入与 TLB 刷新，不包含任何策略逻辑。
* **Ex 负责触发**：Ex 层通过挂载在进程切换路径上的回调，调用上述机制接口完成地址空间切换。
* **解除固定约束**：将 Staging Payload 直接映射进进程专属的地址空间根中，彻底废除 `0x80000000` 固定窗口的早期约束。

---

## 严格防范的范围蔓延（Out of Scope）

为避免过度设计阻碍 Phase B 的交付，以下组件在当前及下一阶段中**明确不予实施**：

* **KPROCESS**：暂不引入内核级进程抽象，除非 Phase B 在处理 CR3 切换机制时发现非其不可。
* **Handle Table / Capability**：对象句柄表和能力模型推迟至 Phase C。
* **正式 Syscall 表面**：面向对象的标准系统调用体系推迟至 Phase D。
* **ELF Loader**：动态可执行文件加载器推迟至 Phase E。