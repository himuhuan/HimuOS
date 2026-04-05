# 修复公告：`user_dual` 销毁竞态与 QEMU 覆盖盲区

**日期：** 2026-04-05  
**严重程度：** Critical  
**状态：** 已修复并完成验证

## 概述

本次 hotfix 同时修复了两个相互耦合的问题：

1. `user_dual` 在 TCG 路径下可能触发 `Failed to resolve dispatch root for KTHREAD` panic。
2. 既有推荐回归路径事实上偏向 host/KVM，导致该问题长期被默认工作流掩盖。

换言之，真正的内核缺陷在 bootstrap raw-exit / teardown 顺序上，但之所以迟迟未暴露，是因为回归工作流没有把不同执行模型的时序差异纳入“必要证据”。

## 影响

- bootstrap 线程可能在其用户态 payload 已销毁后仍短暂保持“可调度且可被识别为 bootstrap-owned”的状态；
- 相同配置在 host/KVM 下可能表现正常，而在 TCG 下崩溃；
- 对 `user_dual` 及类似 timing-sensitive / teardown-sensitive profile，单份 host/KVM 日志不再能证明路径无竞态。

## 根因

### 1. 工作流 / 覆盖缺陷

修复前，`make run` 把 host/KVM 路径写死在运行目标里，而 `scripts/qemu_capture.sh` 也没有显式的执行模型选择能力。结果是：

- 默认回归主要覆盖 host/KVM；
- TCG 不是默认证据的一部分；
- 单次 host/KVM clean-pass 容易被误读为“配置不存在竞态”。

### 2. 内核 teardown 竞态

修复前，`SYS_RAW_EXIT` 会先销毁 bootstrap process 的 staging / address-space payload，再让线程真正走进 `KeThreadExit()`。在这段窗口里：

1. runtime alias 仍然存在；
2. 线程仍可能被 timer preemption 打断；
3. 调度器后续仍可能把该线程当成 bootstrap-owned 线程解析 dispatch root；
4. 但此时进程地址空间已经被销毁，最终触发 panic。

## 修复措施

### Commit `4e19076` — 参数化 QEMU 捕获模式

- `make run` 不再把 KVM 作为硬编码路径，而是通过 `QEMU_ACCEL_MODE` / `QEMU_ACCEL_ARGS` 选择执行模型；
- `scripts/qemu_capture.sh` 新增显式模式：
  - `QEMU_CAPTURE_MODE=host`
  - `QEMU_CAPTURE_MODE=tcg`
  - `QEMU_CAPTURE_MODE=custom`
- 自动化捕获日志不再混入可避免的 `sudo` prompt 噪音；
- `Readme.md` 与 `docs/handoff.md` 已更新为：`user_dual` 等时序敏感 profile 必须同时提供 host 与 TCG 两份证据。

### Commit `9e98afa` — 将 bootstrap payload teardown 延后到 finalizer

- `ExBootstrapAdapterHandleRawExit()` 现在只负责校验 raw-exit handoff，不再当场 teardown payload；
- staging 与 process address-space 的真正销毁保留在 terminated-thread finalizer 路径；
- 因此，“runtime alias 仍然存在，但 payload 已经销毁”的危险状态被消除；
- P3 证据链同步调整为：
  1. `SYS_RAW_EXIT`
  2. thread terminated
  3. finalizer teardown complete
  4. idle/reaper reclaim

## 验证结果

| 场景 | 结果 |
|---|---|
| `test-schedule` + `QEMU_CAPTURE_MODE=host` | 捕获成功，日志可见 host 模式选择 |
| `test-schedule` + `QEMU_CAPTURE_MODE=tcg` | 捕获成功，日志可见 tcg 模式选择 |
| `test-schedule` + `QEMU_CAPTURE_MODE=custom`，并传入 `QEMU_ACCEL_ARGS='-accel tcg' QEMU_CPU_FLAGS=max` | 捕获成功，custom 模式正确接受显式 accel / CPU 参数 |
| workflow 修复后的捕获产物 | 未发现可避免的 `sudo` prompt 噪音 |
| `test-user_dual` + `QEMU_CAPTURE_MODE=host` | 未出现 panic；观测到 2 次 teardown-complete 证据 |
| `test-user_dual` + `QEMU_CAPTURE_MODE=tcg` | 未出现 panic；观测到 2 次 teardown-complete 证据 |

## 更新后的验证口径

对 `user_dual` 以及其他 timing-sensitive / teardown-sensitive profile，后续必须执行：

1. `make clean && bear -- make all ...`
2. `QEMU_CAPTURE_MODE=host bash scripts/qemu_capture.sh ...`
3. `QEMU_CAPTURE_MODE=tcg bash scripts/qemu_capture.sh ...`
4. 若 host 与 TCG 结果不一致，必须在 bug / handoff / fix-report 中显式注明分歧，不能再把 host/KVM 结果当作唯一结论。

## 经验教训

1. **执行模型差异本身就是回归覆盖的一部分。** host/KVM clean-pass 不能替代 timing-sensitive 路径的完整证据。
2. **bootstrap raw-exit 不应在线程仍可被调度时销毁地址空间状态。** 生命周期边界必须跟随 terminated-thread finalizer。
3. **诊断日志必须对应真实生命周期。** 在 teardown 实际发生前就宣称“teardown complete”，只会让竞态分析更困难。

## 受影响文件

- `makefile`
- `scripts/qemu_capture.sh`
- `Readme.md`
- `docs/handoff.md`
- `src/kernel/ke/user_bootstrap_syscall.c`
- `src/kernel/ex/ex_bootstrap_adapter.c`
- `src/kernel/ke/thread/scheduler/scheduler.c`
- `src/include/kernel/ex/ex_bootstrap_adapter.h`
- `src/include/kernel/ex/ex_bootstrap_abi.h`
- `src/kernel/demo/user_hello.c`
