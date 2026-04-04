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

---

# Agent Log: complete-ex-bootstrap-migration

## 阶段计划

agent 正在统筹 `complete-ex-bootstrap-migration` 的串行实施计划。

### Phase 1: Ex Runtime And Launch API (tasks 1.1, 1.2)
- 1.1: 新增 Ex-owned bootstrap runtime 初始化入口，收口 raw syscall trap 初始化与 bootstrap callback 注册
- 1.2: 定义最小 Ex bootstrap create/start/teardown API 与参数类型，对外只暴露 Ex 头文件和 Ex 薄对象

### Phase 2: Demo Launch Cutover (tasks 2.1, 2.2)
- 2.1: 将 `user_hello` 迁移到 Ex create/start API 链路，移除手工 staging attach 与 bootstrap flag 设置
- 2.2: 让 Ex 在 thread start 前建立 bootstrap ownership，使 trampoline 首次进入与 timer observe 可以走 Ex 查询面

### Phase 3: Ke Residue Removal (tasks 3.1, 3.2, 3.3, 3.4)
- 3.1: 扩展最小 bootstrap callback contract，引入 thread ownership query，并收口 finalizer/reaper 判定
- 3.2: 将 `SYS_RAW_EXIT` clean-path teardown 改成 Ex-owned lifecycle helper，并保证 destroy 失败时仍清理 Ex wrapper / registry
- 3.3: 删除 `KTHREAD_FLAG_BOOTSTRAP_USER` 与 `UserBootstrapContext`，收掉 scheduler、timer、demo、init 中的直接读取
- 3.4: 通过 Ex-owned facade 隐藏 `user_bootstrap*` 的公开 surface；如无必要，不做物理迁移

### Phase 4: Validation And Docs (tasks 4.1, 4.2, 4.3)
- 4.1: 按 test-user_hello flavor 执行 clean rebuild
- 4.2: 用 `scripts/qemu_capture.sh` 采集日志并核对 evidence chain 与 idle/reaper reclaim
- 4.3: 同步更新能力与设计文档，说明 Ex 已成为 bootstrap launch/init/teardown owner

## 实施记录

### Phase 1 开始
agent 正在启动 Phase 1: Ex Runtime And Launch API 实施。
- 当前策略：先以最小改动新增 Ex facade 与 runtime init 收口，不提前删除 Ke 残留字段。
- 当前检查结果：`user_hello` 仍直接创建 staging、设置 `KTHREAD_FLAG_BOOTSTRAP_USER` 并 attach 到 `KTHREAD`；`InitKernel()` 仍直接调用 `KeUserBootstrapRawSyscallInit()`。
- reviewer 首轮 BLOCK：新公开的 Ex bootstrap 句柄在 start 后会被内核侧异步释放，post-start ownership contract 不安全；同时 `ExBootstrapTeardownThread()` 对已启动线程会过早销毁 staging。
- 当前修复方向：收紧 public API ownership 语义，让 create/start 显式转移句柄所有权，并把 `ExBootstrapTeardownThread()` 限制为 pre-start cancel path。
- impl 已完成 Phase 1：新增 `ExBootstrapInit()`、bootstrap-scoped Ex process/thread create/start/teardown facade、私有 Ex wrapper 共享状态，并将 `InitKernel()` 切换为单一 Ex 初始化入口。
- reviewer 二次 PASS：确认 started-thread 不会再被 public teardown 提前销毁，public handle 的 ownership transfer contract 已收紧到安全边界。
- 构建验证：`make clean && bear -- make all` 成功。
- commit: `phase-1: Ex bootstrap runtime and launch facade` (`4daafbf`)

### Phase 2 开始
agent 正在启动 Phase 2: Demo Launch Cutover 实施。
- 当前策略：将 `user_hello` 切到 Ex facade，同时只把 trampoline 和 timer 的 bootstrap 判定切到 Ex ownership query；finalizer/reaper 留到 Phase 3 统一处理。
- impl 已完成 Phase 2：`user_hello` 已迁移到 Ex bootstrap facade，Ke bootstrap callback contract 增加 thread ownership query，trampoline 与 timer observe 改为通过 Ex registry 判断。
- reviewer PASS：确认本阶段改动在当前单 bootstrap-thread 模型下自洽，未发现阻塞问题。
- 验证结果：`make clean && bear -- make all BUILD_FLAVOR=test-user_hello HO_DEMO_TEST_NAME=user_hello HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO` 成功；`qemu_capture.sh` 日志锚点保持不变，包含 `enter user mode`、`timer from user #1/#2`、`invalid raw write rejected`、`hello write succeeds`、`SYS_RAW_EXIT`、`bootstrap teardown complete`、`idle/reaper reclaimed`。
- commit: `phase-2: cut over user_hello launch to Ex facade` (`86c974c`)

### Phase 3 开始
agent 正在启动 Phase 3: Ke Residue Removal 实施。
- 当前策略：删除 `KTHREAD_FLAG_BOOTSTRAP_USER` 与 `UserBootstrapContext`，让 staging/ownership 全部通过 Ex 内部 registry 查询；clean `SYS_RAW_EXIT` 由 Ex helper 销毁 staging，但保留最小 Ex wrapper 直到 finalizer/reaper 消费完 bootstrap identity。
- impl 已完成 Phase 3：新增 Ex-owned bootstrap ABI 头；finalizer/reaper 切到 Ex ownership query；`SYS_RAW_EXIT` 改为 Ex helper teardown；`KTHREAD_FLAG_BOOTSTRAP_USER` 与 `UserBootstrapContext` 已删除；demo 和 arch 不再直接 include `kernel/ke/user_bootstrap.h`。
- reviewer PASS：确认 Phase 3 在当前单 bootstrap-thread registry 模型下自洽，未发现阻塞问题。
- 静态校验：残留字段全仓引用已清空，受影响文件诊断无错误。
- commit: `phase-3: remove Ke bootstrap residue` (`7b050b2`)

### Phase 4 开始
agent 正在启动 Phase 4: Validation And Documentation Sync 实施。
- 4.1: `make clean && bear -- make all BUILD_FLAVOR=test-user_hello HO_DEMO_TEST_NAME=user_hello HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO` 成功，生成 `build/kernel/test-user_hello/bin/kernel.bin`。
- 4.2: `BUILD_FLAVOR=test-user_hello HO_DEMO_TEST_NAME=user_hello HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO bash scripts/qemu_capture.sh 30 /tmp/himuos-user-hello.log` 已重跑；串口日志保持 `enter user mode`、`timer from user #1/#2`、`P1 gate armed`、`invalid raw write rejected`、`hello write succeeds`、`SYS_RAW_EXIT`、`bootstrap teardown complete`、`idle/reaper reclaimed` 锚点。脚本按 bounded capture 预期以 124 结束。
- 4.3: 已同步更新 `docs/current-ability.md` 与 `docs/draft/userspace.md`，明确 Ex 已成为 bootstrap launch/init/teardown owner，正式地址空间语义从 Phase B 才开始承接。
- reviewer PASS：文档与当前实现一致，未发现阻塞问题。
- 所有 tasks 完成

