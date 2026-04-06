# Agent Log

## Change: `complete-demo-shell-demo-flow`  —  2026-04-06

---

## 1. 目标

将 [`docs/todo.md`](docs/todo.md) 中的 `P3` 收口为新的 OpenSpec change `complete-demo-shell-demo-flow`，并按串行阶段完成 proposal / design / specs / tasks、实现、验证、reviewer-fix 到交付的完整闭环。

P3 的直接目标是把已有的控制面和输入闭环收口成可演示的三程序交互切片：`hsh -> & tick1s -> calc -> q -> hsh`，在不引入 kill、FS、PATH、通用 loader、完整 tty discipline 或独立 PID allocator 的前提下，证明后台持续输出与前台 REPL 交错成立。

## 2. 约束

- 最小修改原则。P3 不新增 syscall，内核改动限于复用现有 `demo_shell` runtime helper 和必要 bookkeeping 调整。
- 不引入 `kill`、FS、PATH、通用 loader、完整 tty discipline 或独立 PID allocator。任何超出范围的能力推后到 P4 单独收口。
- `HO_HSH_MAX_JOBS = 4` 是 P3 job 表的硬上限，不扩展通用 job control 语义。
- 构建遵守仓库约束：一律从 `make clean; bear -- make all ...` 开始，不把 `make kernel`、`make efi` 当作默认入口。
- 运行和验证一律优先通过 `scripts/qemu_capture.sh`，不使用 `make run` 或 `make test` 作为默认路径。
- `openspec/changes/complete-demo-shell-demo-flow/` 下的 proposal / design / specs / tasks 按约定不纳入最终 git commit。

---

## 3. OpenSpec 方案摘要

**Change 名称：** `complete-demo-shell-demo-flow`

**Why：** P0 冻结了官方演示范围，P1 补齐了运行期输入闭环，P2 让 `hsh` 可以通过窄控制面派生 `calc` 和 `tick1s`。但 `calc` 仍是一次性 readline skeleton，`tick1s` 仍是 bounded ticker，演示故事尚未真正讲完。P3 需要单独收口，以最小改动完成三程序交互的实质演示。

**What（核心范围）：**

- `hsh`：收口为 P3 最小命令面，命令集固定为 `help`、`exit`、`calc`、`& tick1s`；在存在 live background job 时拒绝 `exit`，避免 P4 kill 语义提前渗入。
- `calc`：从一次性 readline skeleton 升级为前台循环式 RPN REPL，支持整数、`+`、`-`、`*`、`/`、`q` 退出，带固定大小整数栈（`HO_CALC_STACK_CAPACITY = 16`）。
- `tick1s`：从 bounded ticker 升级为持续 1 秒周期输出的后台用户程序，只用 `sleep_ms(1000)` + `write("TICK!\n")` 无限循环。
- 验证合同升级为证明 `calc` 会话期间确实出现后台 `TICK!`，且 `q` 退出后控制权回到 `hsh`。

**Tasks 阶段划分（对应 [`openspec/changes/complete-demo-shell-demo-flow/tasks.md`](openspec/changes/complete-demo-shell-demo-flow/tasks.md)）：**

| 阶段 | 内容 |
|------|------|
| Phase 1 | Tighten `hsh` P3 semantics：最小命令面、job 表最小化、live background job 下拒绝 `exit` |
| Phase 2 | Upgrade `calc` into the minimal RPN REPL：提示符 `calc>`、结果锚点 `[CALC] result=`、错误锚点 `[CALC] error=` |
| Phase 3 | Upgrade `tick1s` + preserve foreground handoff：持续 ticker、经验证 `demo_shell` runtime helper 无需修改 |
| Phase 4 | Refresh validation evidence：更新 `demo_shell.plan`，完成 `demo_shell` 与 `user_input` 的 host/tcg 双路径验证 |

---

## 4. 分阶段实施记录

### Phase 1：hsh 最小命令面与 job 表收口

`hsh` 的 P3 实现以 `#if defined(HO_DEMO_TEST_DEMO_SHELL)` 做 profile 分离，保证非 demo_shell profile 下的 P1 skeleton 不受影响。P3 路径下主要改动：

- job 表收口为 `HO_HSH_MAX_JOBS = 4`，条目含 `Pid`、`Name`、`Background`、`Alive` 四字段，不扩展通用 job control。
- `exit` 命令增加 live background job 检查：遍历 job 表，若有 `Background && Alive` 条目则打印 `[HSH] exit refused: live background job` 并返回提示符，不执行退出。这是为了在 `kill` 尚未落地时避免把后台 ticker 悬空。
- `& tick1s` 命令通过 `HoUserSpawnBuiltin` + 后台标志完成，spawn 成功后立即打印 `[HSH] started pid=<N> name=tick1s bg=1`。
- `calc` 命令通过 `HoUserSpawnBuiltin` + 前台标志完成，等待 `HoUserWaitPid` 返回后 foreground owner 恢复到 `hsh`。

关键输出锚点：`hsh>`、`[HSH] started pid=... name=tick1s bg=1`、`[HSH] exit refused: live background job`。

### Phase 2：calc 升级为最小 RPN REPL

同样以 `#if defined(HO_DEMO_TEST_DEMO_SHELL)` 做 profile 分离。P3 路径下：

- 每次 `readline` 读取一整行，只接受三类输入：十进制整数（压栈）、操作符（`+`、`-`、`*`、`/`，弹两个操作数并压回结果）、`q`（退出循环）。
- 空行只重显提示符，不报错。错误路径覆盖栈下溢 (`stack_underflow`)、栈溢出 (`stack_overflow`)、非法 token (`invalid_token`)、除零 (`divide_by_zero`)。
- 提示符固定为 `calc>`，结果行固定为 `[CALC] result=<N>`，错误行固定为 `[CALC] error=<reason>`。
- `q` 退出后程序正常调用 `SYS_EXIT`，foreground owner 由 `demo_shell` runtime helper 通过 `wait_pid` 路径恢复。

P1 skeleton 路径（非 demo_shell profile）在 `#else` 分支中保留：单次 readline + 回显 + 退出，不做任何 REPL 循环。

### Phase 3：tick1s 升级为持续 ticker 并验证 foreground 恢复

[`src/user/tick1s/main.c`](src/user/tick1s/main.c) 的改动是最小的：把有界循环改为 `for(;;)` 无限循环。程序首先调用 `HoUserWaitForP1Gate()` 等待 P1 gate，然后持续执行 `sleep_ms(1000)` + `write("TICK!\n")`。任何系统调用失败直接 `HoUserAbort()`。

关于 `demo_shell` runtime helper：经过实际验证，现有 `demo_shell_runtime.c` 中的 `wait_pid` 恢复路径已足够，foreground owner 在 `calc` 退出后可稳定回到 `hsh`，无需对 runtime helper 做任何代码改动。

### Phase 4：更新 demo_shell.plan 并完成双路径验证

[`scripts/input_plans/demo_shell.plan`](scripts/input_plans/demo_shell.plan) 更新为 `wait_for` 锚点驱动的 P3 流程，不依赖固定时序键盘 timing：

```
wait_for hsh>              →  text & tick1s + key ret
wait_for name=tick1s bg=1  →  wait_for hsh>
text calc + key ret        →  wait_for calc>
text 3    + key ret        →  wait_for [CALC] result=3
text 4    + key ret        →  wait_for [CALC] result=4
text +    + key ret        →  wait_for [CALC] result=7  →  wait_for TICK!
text q    + key ret        →  wait_for hsh>
```

两次完整构建分别针对 `demo_shell` 和 `user_input` flavor，每个 flavor 分别执行 host 和 tcg 捕获，共 4 次运行。

---

## 5. Reviewer 与修复记录

### 第一轮 reviewer 阻塞项

**阻塞项：`calc` REPL 全局化，打穿 `user_input` P1 合同。**

初版实现中，`calc/main.c` 的 RPN REPL 循环直接写在 `main()` 中，没有 profile guard。结果在 `user_input` profile 下，`calc` 也进入了多轮 readline 循环，违反了 P1 "calc 只做一次性 readline + echo + exit" 的既有合同，破坏了 `user_input` 的 `[CALC] 3 4 +` 锚点和稳定退出链。

**修复动作：**

在 [`src/user/calc/main.c`](src/user/calc/main.c) 中以 `#if defined(HO_DEMO_TEST_DEMO_SHELL)` 包裹 RPN REPL 路径：

- `demo_shell` profile：进入 P3 REPL 循环（整数栈、操作符处理、`q` 退出）。
- 非 `demo_shell` profile（即 `user_input`）：走 `#else` 分支，保留 P1 skeleton——单次阻塞式 readline + 原样回显 + `HoUserAbort()`-on-error + 正常退出。

修复后分别对 `demo_shell` 和 `user_input` 重新完整构建并捕获日志，两个 profile 的锚点均通过。

### 第二轮 reviewer 结论

**无阻塞项。**

Non-blocking observations（记录在案，不阻塞本次交付）：

1. `demo_shell.plan` 目前没有覆盖 `exit refused` 场景（有 live background job 时输入 `exit`）。当前 plan 在 `q` 退出 calc 后直接结束，不驱动 `exit`，因此这条拒绝路径的脚本化验证留待 P4 补充。

2. P4 的后台 job 状态转换（`kill`、`exit` 在无 live job 时的正常退出、job 表 `Alive` 清零后的 `exit` 通过路径）需要独立收口，不在本次 P3 交付范围内。

---

## 6. 验证摘要

### 6.1 demo_shell — host / tcg

**构建：**
```bash
make clean && bear -- make all BUILD_FLAVOR=test-demo_shell \
    HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL
```

**运行（host / tcg 各一次）：**
```bash
BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell \
    HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL \
    QEMU_SENDKEY_PLAN=scripts/input_plans/demo_shell.plan \
    bash scripts/qemu_capture.sh 25 /tmp/himuos-demo-shell-host.log
```

**必须出现的日志锚点：**

| 锚点 | 含义 |
|------|------|
| `hsh>` | hsh 提示符正常出现 |
| `[HSH] started pid=... name=tick1s bg=1` | tick1s 后台 spawn 成功，job 表记录 |
| `calc>` | calc 进入前台 REPL |
| `[CALC] result=3` | 整数 `3` 压栈计算正确 |
| `[CALC] result=4` | 整数 `4` 压栈计算正确 |
| `[CALC] result=7` | `3 4 +` 操作结果正确 |
| `TICK!` | calc 运行期间后台 tick1s 至少出现一次，证明后台/前台交错成立 |
| `hsh>`（第二次） | calc 退出后控制权回到 hsh，foreground owner 恢复 |

### 6.2 user_input — host / tcg

**构建：**
```bash
make clean && bear -- make all BUILD_FLAVOR=test-user_input \
    HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT
```

**必须出现的日志锚点：**

| 锚点 | 含义 |
|------|------|
| `foreground -> hsh` | hsh 获得前台 ownership |
| `hsh>` | hsh 提示符正常出现 |
| `[HSH] hello` | hsh 在 P1 skeleton 下正确回显输入 |
| `[HSH] handoff` | hsh 完成控制权移交 |
| `foreground -> calc` | calc 获得前台 ownership |
| `[CALC] 3 4 +` | calc P1 skeleton 单次回显正确 |
| `SYS_EXIT` / 稳定退出链 | calc 正常退出，无 panic 或 fault |

### 6.3 关于返回码 124

所有交互性 profile 验证中，`scripts/qemu_capture.sh` 均因 watchdog 超时返回 `124`。这是预期行为：`demo_shell` profile 中 `tick1s` 无限运行，QEMU 不会自行退出，watchdog 到时后终止进程组。**只要上述日志锚点全部出现且无 panic / fault / BSOD，`124` 不视为失败。**

---

## 7. 主要修改文件范围

| 文件 | 变更说明 |
|------|----------|
| [`src/user/hsh/main.c`](src/user/hsh/main.c) | P3 profile 路径：job 表、exit refused、& tick1s 后台 spawn、calc 前台派生 |
| [`src/user/calc/main.c`](src/user/calc/main.c) | demo_shell profile：RPN REPL；非 demo_shell：保留 P1 skeleton |
| [`src/user/tick1s/main.c`](src/user/tick1s/main.c) | bounded ticker → 持续 `for(;;)` 一秒 ticker |
| [`scripts/input_plans/demo_shell.plan`](scripts/input_plans/demo_shell.plan) | 更新为 P3 `wait_for` 锚点驱动流程 |
| [`openspec/changes/complete-demo-shell-demo-flow/`](openspec/changes/complete-demo-shell-demo-flow/) | proposal / design / specs / tasks（不入 git） |

内核侧（`demo_shell_runtime.c`、`demo_shell.c`、`tick1s_artifact_bridge.c`、`user_bootstrap_syscall.c`）在 P3 阶段经验证无需改动，沿用 P2 交付的实现。

---

## 8. 风险与后续

**Residual risks（当前交付直接相关）：**

- `tick1s` 无限运行，P4 `kill` 尚未落地。当前演示依赖 watchdog 外部结束进程组；在 P4 完成前，`demo_shell` profile 不能要求 clean-pass 自然收尾。
- `calc` 退出后 foreground owner 恢复依赖 `demo_shell_runtime.c` 中的 `wait_pid` 路径。host/tcg 均已验证稳定，但该路径没有覆盖 `wait_pid` 超时或 child 未正常退出的情况；P4 引入 `kill` 后需重新评估。

**P4 后续工作（不在本次交付范围，需单独 change 收口）：**

- `kill_pid(pid)` 语义落地：`hsh` 内部实现 `kill <pid>` 命令，针对 job 表中的后台进程，复用现有 terminate/finalizer/reaper 链。
- 后台 job 状态转换：`kill` 成功后将 job 表条目标记 `Alive = false`，使 `exit` 检查可以通过，驱动 shell 正常退出。
- `demo_shell.plan` 补充 `exit refused` 与 `exit allowed`（kill 后）的脚本化覆盖。
- 冻结官方 demo 演示脚本和完整锚点集，作为答辩/录屏的稳定基线。
