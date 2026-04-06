# Agent Log

## 1. 目标

本次工作的目标是将 [`docs/todo.md`](/home/liuhuan/HimuOS/docs/todo.md) 中的 `P2` 收口为一个新的 OpenSpec change：`introduce-demo-shell-control-plane`，并按串行阶段完成从 proposal 到实现、验证、审查和交付的完整闭环。

## 2. 约束

- 以最小改动为原则，不引入完整 shell、FS、PATH、loader、完整 tty 或通用进程模型。
- `kill_pid` 明确保留给后续阶段，不在本次 `P2` 里落地语义。
- 保持 `user_input` 作为 `P1` 输入/foreground-handoff 的回归锚点，不让 `P2` 改动破坏既有证据链。
- `openspec/` 内容是有意排除版本控制的；proposal/design/specs/tasks 已更新，但不会进入最终 git commit。

## 3. 分阶段实施记录

### Phase 1: OpenSpec proposal/design/specs/tasks

- 新建 change：`openspec/changes/introduce-demo-shell-control-plane/`
- 落盘 `proposal.md`、`design.md`、`tasks.md` 以及对应 specs。
- 将范围收敛为 demo-shell 的窄控制面：
  - `spawn_builtin(program_id, flags)`
  - `wait_pid(pid)`
  - `sleep_ms(ms)`
- 明确 `kill_pid` 延后到后续阶段，避免把范围拉入线程终止/信号语义。

### Phase 2: ABI / syscall / runtime helper

- 在 ABI 中新增：
  - `SYS_SPAWN_BUILTIN`
  - `SYS_WAIT_PID`
  - `SYS_SLEEP_MS`
  - builtin program id
  - spawn flags
- 在 [`src/user/libsys.h`](/home/liuhuan/HimuOS/src/user/libsys.h) 中新增稳定包装：
  - `HoUserSpawnBuiltin(...)`
  - `HoUserWaitPid(...)`
  - `HoUserSleepMs(...)`
- 在 [`src/kernel/ke/user_bootstrap_syscall.c`](/home/liuhuan/HimuOS/src/kernel/ke/user_bootstrap_syscall.c) 中完成 syscall 分发接入。
- 新增 [`src/include/kernel/demo_shell.h`](/home/liuhuan/HimuOS/src/include/kernel/demo_shell.h) 与 [`src/kernel/demo/demo_shell_runtime.c`](/home/liuhuan/HimuOS/src/kernel/demo/demo_shell_runtime.c)，提供最小 child table 与 demo-shell runtime helper。

### Phase 3: profile-aware hsh / tick1s / demo_shell profile

- `hsh` 改为 profile-aware：
  - `user_input` 下继续保留 `P1` skeleton 行为
  - `demo_shell` 下启用最小 REPL，支持 `help`、`exit`、`calc`、`& tick1s`
- 新增 bounded `tick1s` 用户程序：
  - [`src/user/tick1s/main.c`](/home/liuhuan/HimuOS/src/user/tick1s/main.c)
  - [`src/kernel/demo/tick1s_artifact_bridge.c`](/home/liuhuan/HimuOS/src/kernel/demo/tick1s_artifact_bridge.c)
- 新增 `demo_shell` profile：
  - [`src/kernel/demo/demo_shell.c`](/home/liuhuan/HimuOS/src/kernel/demo/demo_shell.c)
- 更新 makefile，使 user build 继承 profile define，并将 `tick1s` 与 `demo_shell` profile 纳入构建与测试矩阵。

### Phase 4: sendkey plan 与 host/tcg 验证

- 新增 [`scripts/input_plans/demo_shell.plan`](/home/liuhuan/HimuOS/scripts/input_plans/demo_shell.plan)
- 更新 makefile 中的显式 flavor-and-capture 用法说明
- 完成以下验证：
  - `demo_shell` host
  - `demo_shell` tcg
  - `user_input` host
  - `user_input` tcg

## 4. 问题与修复过程

### 初版故障

在 `demo_shell` 的首次 host 验证中，sendkey 已成功打入 `& tick1s`，日志明确显示：

- `SYS_SPAWN_BUILTIN succeeded`
- child PID 已返回

但随后新起的 child 在线程首次进入用户态时立即触发 `#PF`，表现为：

- `& tick1s` 输入已完成
- shell 开始回显 started message
- child 进入用户态后立即 fault

### 定位过程

- 先按协议采集 `qemu_capture.sh` 运行日志
- 观察到 fault 发生在 spawn 成功之后，而不是 sendkey/输入链路之前
- 初步判断问题位于“在 shell 的 syscall 上下文里直接启动 builtin child”这条路径，而不是 `tick1s` 文本命令解析本身

### 修复动作

将 builtin spawn 的实际启动动作收口到 joinable kernel worker 路径：

- `KeDemoShellSpawnBuiltin()` 不再直接在当前 shell syscall 现场完成 child 启动
- 改为创建一个 joinable kernel worker
- 由 worker 在 kernel context 中完成：
  - `ExBootstrapCreateProcess`
  - `ExBootstrapCreateThread`
  - `ExBootstrapStartThread`
  - child table 填充
- 主线程同步 `join` 该 worker，拿回 PID / 状态

再次执行 `demo_shell` host / tcg 验证后，故障消失，完整链路通过。

## 5. 验证摘要

### demo_shell

验证命令：

- `BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/demo_shell.plan bash scripts/qemu_capture.sh 25 /tmp/himuos-demo-shell-host.log`
- `BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL QEMU_CAPTURE_MODE=tcg QEMU_SENDKEY_PLAN=scripts/input_plans/demo_shell.plan bash scripts/qemu_capture.sh 25 /tmp/himuos-demo-shell-tcg.log`

日志锚点确认了以下事件均已成立：

- profile 选中：`[DEMO] Selected profile: demo_shell`
- 后台 spawn tick1s：`SYS_SPAWN_BUILTIN succeeded ... program=3`
- 前台 spawn calc：`SYS_SPAWN_BUILTIN succeeded ... program=2`
- `calc>` prompt 与 `[CALC] 3 4 +`
- `SYS_WAIT_PID succeeded`
- `SYS_SLEEP_MS succeeded`
- `TICK!` 共 3 次
- `bootstrap teardown complete thread=1`
- `foreground owner=0`

### user_input

验证命令：

- `BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/user_input.plan bash scripts/qemu_capture.sh 20 /tmp/himuos-user-input-host.log`
- `BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT QEMU_CAPTURE_MODE=tcg QEMU_SENDKEY_PLAN=scripts/input_plans/user_input.plan bash scripts/qemu_capture.sh 20 /tmp/himuos-user-input-tcg.log`

日志锚点确认了以下事件仍然成立：

- profile 选中：`[DEMO] Selected profile: user_input`
- `foreground -> hsh`
- `foreground -> calc`
- `[HSH] handoff`
- `[CALC] 3 4 +`
- `bootstrap teardown complete thread=2`
- `foreground owner=0`

补充说明：

- `calc` 在 foreground handoff 之前多次收到 `SYS_READLINE rejected ... EC_INVALID_STATE` 是预期行为，这正是 `P1` “只有 foreground reader 才能读取输入”的合同体现，不视为回归。

### 关于返回码 124

四次 `qemu_capture.sh` 运行都因为超时终止返回 `124`。这是当前脚本的预期行为之一：profile 已经 clean-pass 收尾并回到 idle，但 QEMU 不会自行退出，因此 watchdog 在设定时间到达后结束进程组。

本次验证以日志锚点为准，日志中已经包含完整的 clean-pass 证据链，因此 `124` 不视为失败。

## 6. 审查记录

- 尝试拉起 reviewer 子代理进行阻塞性审查
- reviewer 子代理因额度限制失败，无法继续执行
- 随后由主代理执行人工阻塞审查，重点覆盖：
  - spawn worker / child table 生命周期
  - foreground owner 恢复
  - `user_input` 兼容性
  - makefile wiring 与 artifact 嵌入
  - failure path 与回归风险
- 审查结论：未发现阻塞提交的问题

## 7. 最终交付文件范围

### 主要修改文件

- [`makefile`](/home/liuhuan/HimuOS/makefile)
- [`scripts/input_plans/user_input.plan`](/home/liuhuan/HimuOS/scripts/input_plans/user_input.plan)
- [`scripts/input_plans/demo_shell.plan`](/home/liuhuan/HimuOS/scripts/input_plans/demo_shell.plan)
- [`src/include/kernel/ex/ex_bootstrap_abi.h`](/home/liuhuan/HimuOS/src/include/kernel/ex/ex_bootstrap_abi.h)
- [`src/include/kernel/demo_shell.h`](/home/liuhuan/HimuOS/src/include/kernel/demo_shell.h)
- [`src/kernel/ke/user_bootstrap_syscall.c`](/home/liuhuan/HimuOS/src/kernel/ke/user_bootstrap_syscall.c)
- [`src/kernel/demo/demo.c`](/home/liuhuan/HimuOS/src/kernel/demo/demo.c)
- [`src/kernel/demo/demo_internal.h`](/home/liuhuan/HimuOS/src/kernel/demo/demo_internal.h)
- [`src/kernel/demo/demo_shell_runtime.c`](/home/liuhuan/HimuOS/src/kernel/demo/demo_shell_runtime.c)
- [`src/kernel/demo/demo_shell.c`](/home/liuhuan/HimuOS/src/kernel/demo/demo_shell.c)
- [`src/kernel/demo/tick1s_artifact_bridge.c`](/home/liuhuan/HimuOS/src/kernel/demo/tick1s_artifact_bridge.c)
- [`src/user/libsys.h`](/home/liuhuan/HimuOS/src/user/libsys.h)
- [`src/user/hsh/main.c`](/home/liuhuan/HimuOS/src/user/hsh/main.c)
- [`src/user/tick1s/main.c`](/home/liuhuan/HimuOS/src/user/tick1s/main.c)

### 非版本控制交付

- `openspec/changes/introduce-demo-shell-control-plane/` 下的 proposal/design/specs/tasks 已更新，但按约定不纳入 git。
