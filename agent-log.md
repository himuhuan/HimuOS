# Agent Log: introduce-ex-bootstrap-adapter

## 阶段计划

agent 正在统筹 `introduce-ex-bootstrap-adapter` 的四阶段实施计划。

### Phase 1: Contract & Scaffolding (tasks 1.1, 1.2)
- 1.1: 在 `user_hello` 相关代码注释中固定 clean-pass 验收锚点，明确本 change 只允许边界重构
- 1.2: 新建 `src/kernel/ex/` 和 `src/include/kernel/ex/` 目录结构，创建骨架头文件和源文件，更新 makefile

### Phase 2: Thin Ex Ownership Layer (tasks 2.1, 2.2)
- 2.1: 定义 `EX_PROCESS` / `EX_THREAD` 最小结构体与辅助接口
- 2.2: 将 bootstrap staging 创建/关联/生命周期聚拢到 Ex 薄壳对象

### Phase 3: Ke Callback Decoupling (tasks 3.1, 3.2, 3.3)
- 3.1: 在 Ke 中引入 `KE_BOOTSTRAP_CALLBACKS` 注册合同（enter / finalize / timer_observe）
- 3.2: scheduler.c 中硬编码 bootstrap 分支改为回调分发
- 3.3: timer.c 中硬编码观察逻辑改为回调分发 + fail-fast

### Phase 4: Validation & Documentation Sync (tasks 4.1, 4.2, 4.3)
- 构建验证、运行 profile 验证 evidence chain、文档同步

---

## 实施记录

### Phase 1 开始
agent 正在启动 Phase 1: Contract & Scaffolding 实施。
- impl 完成 1.1（anchor comment）、1.2（目录骨架 + makefile）
- reviewer PASS
- commit: `phase-1: contract anchor and Ex scaffold`

### Phase 2 开始
agent 正在启动 Phase 2: Thin Ex Ownership Layer 实施。
- impl 完成 2.1（EX_PROCESS / EX_THREAD 结构定义）、2.2（staging 生命周期聚拢到 Ex）
- reviewer 首次 BLOCK：FinalizeThread 在 staging destroy 失败时提前返回导致 Ex 对象泄漏
- 修复：移除 early return，始终清理 Ex 对象，仅传播错误状态
- reviewer 二次 PASS
- commit: `phase-2: thin Ex ownership layer (EX_PROCESS / EX_THREAD)`

### Phase 3 开始
agent 正在启动 Phase 3: Ke Callback Decoupling 实施。
- impl 完成 3.1（bootstrap_callbacks.h/c 注册合同）、3.2（scheduler 回调分发）、3.3（timer 回调分发）
- scheduler.c / timer.c 不再 `#include <kernel/ke/user_bootstrap.h>`
- Ex 注册三个回调实现，init.c 调用 ExBootstrapAdapterInit
- reviewer PASS
- commit: `phase-3: Ke callback decoupling (enter / finalize / timer_observe)`

### Phase 4 开始
agent 正在启动 Phase 4: Validation & Documentation Sync 实施。
- 4.1: `make clean; bear -- make all BUILD_FLAVOR=test-user_hello ...` 构建成功
- 4.2: `qemu_capture.sh` evidence chain 全部 10 个锚点验证通过
- 4.3: 更新 `docs/current-ability.md` 反映 Ex 适配层与回调收口
- 所有 tasks 完成

