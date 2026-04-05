# Agent Log — `stabilize-ex-facing-userspace-abi`

> **变更标识:** `stabilize-ex-facing-userspace-abi`
> **最终提交:** `1caa5bf` (HEAD → main)
> **执行日期:** 2026-04-05

---

## 1. 变更目标

本次变更（P4 阶段）的核心目标是将 HimuOS 编译型用户程序的公开接口收口为一条清晰、有文档保障的稳定 ABI，同时明确划定正式 Ex-facing 路径与 raw/bootstrap-only 内部路径的边界。

在变更之前，`src/user/libsys.h` 直接泄漏 raw syscall 号、trap 向量、seed 固定偏移与 raw exit 语义；`crt0.S` 的 `_start` 硬编码 `SYS_RAW_EXIT` 作为启动返回路径；`user_counter` 直接使用 bootstrap 魔法常量，导致“正式用户 ABI”与“bootstrap-only raw lane”的边界在代码与叙事两个层面都是混乱的。

变更范围被有意约束在最小面：
- 仅新增一个最小 `SYS_EXIT` 操作，复用现有 `int 0x80` 陷入入口与现有退出 handoff/teardown 路径；
- 将 `libsys.h` 确立为编译型用户程序唯一支持的公开入口；
- `user_hello` **保留** raw/bootstrap 回归锚点角色，不在本次变更中整体改写；
- 明确禁止引入新的 trap 框架、新的 syscall 路径或任何 UAPI header 目录重构。

---

## 2. 串行实施摘要

| 阶段 | Commit | 主要工作 |
|------|--------|----------|
| **P4.1 — ABI 表面稳定化** | `819d1c9` | 新增正式 `SYS_EXIT`；重构 `libsys.h` 成为公开 ABI 表面；更新 `crt0.S` 以正式 exit contract 替代 raw exit；更新 syscall 分发层加入 `SYS_EXIT` 处理与 reserved 参数校验；同步 ABI 常量/注释与 Ex bootstrap adapter |
| **P4.2 — 编译型用户程序迁移** | `1caa5bf` | 将 `user_counter` 切换到稳定包装层；将 `user_hello` raw 访问收进命名 helper，消除用户源码中的裸魔法常量；新增 `HoUserRawProbeGuardPageByte` 等 raw-sentinel 专用 helper |

---

## 3. 关键实现点

### 3.1 `SYS_EXIT` 正式退出 contract（`819d1c9`）

在 `src/include/kernel/ex/ex_bootstrap_abi.h` 中新增：

```c
#define SYS_EXIT  (KE_USER_BOOTSTRAP_CAPABILITY_SYSCALL_BASE + 3U)
```

调用约定为 `SYS_EXIT(exit_code, reserved0, reserved1)`，其中 `reserved0` 与 `reserved1` 由内核校验，非零时返回 `EC_ILLEGAL_ARGUMENT`。实现路径（`src/kernel/ke/user_bootstrap_syscall.c`）中新增 `KiHandleExit()`，它调用 `KiPerformBootstrapExit()`，后者与 `SYS_RAW_EXIT` 的 `KiHandleRawExit()` 共用同一底层退出 handoff/teardown 机制（`ExBootstrapAdapterHandleExit()`），保证两条 exit lane 的终止语义一致，不引入第二条 exit pipeline。

日志锚点方面，`SYS_EXIT` 触发 `[USERBOOT] SYS_EXIT`，`SYS_RAW_EXIT` 触发 `[USERBOOT] SYS_RAW_EXIT`，两条路径均以 `[USERBOOT] bootstrap teardown complete` 作为终止证据。

### 3.2 `crt0.S` 公开启动/返回路径（`819d1c9`）

`src/user/crt0.S` 的 `_start` 入口在 `main` 返回后，将返回值写入 `%edi`，以 `$SYS_EXIT` 发起 `int $KE_USER_BOOTSTRAP_SYSCALL_VECTOR` 陷入。移除了对 `SYS_RAW_EXIT` 的直接引用，并在注释中显式说明当前寄存器约定（`RAX` 传 syscall number/return value，`RDI/RSI/RDX` 传前三个参数）。`ud2` 留作不可达保护。

### 3.3 `libsys.h` 公开 ABI 表面（`819d1c9` + `1caa5bf`）

`src/user/libsys.h` 以注释块形式明确声明当前 `int 0x80` 的完整调用约定、负错误码返回合同，以及成功返回值的含义。对外暴露的稳定 wrapper 包括：

| 函数 | 角色 |
|------|------|
| `HoUserSyscall3(num, a0, a1, a2)` | 通用 Ex-facing syscall 入口 |
| `HoUserCurrentCapabilitySeedBlockIsValid()` | Seed block 校验 helper |
| `HoUserCapabilitySeedBlock()` / `HoUserStdoutHandle()` | Seed 与 handle 访问 |
| `HoUserWrite(handle, buf, len)` | capability-checked write |
| `HoUserWriteStdout(buf, len)` | 对 stdout handle 的封装 write |
| `HoUserExit(exit_code)` | 正式 `SYS_EXIT` 包装，`HO_NORETURN` |
| `HoUserWaitForP1Gate()` | P1 mailbox 门栅等待 |
| `HoUserAbort()` | 不可恢复异常退出 |

与此同时，以下 helper 被明确标注为 **raw/bootstrap-only**，不属于稳定公开 ABI：

| 函数 | 用途 |
|------|------|
| `HoUserRawSyscall3(num, a0, a1, a2)` | raw syscall 入口 |
| `HoUserRawWrite(buf, len)` | raw `SYS_RAW_WRITE` 封装 |
| `HoUserRawProbeGuardPageByte()` | guard page 探测（负向验证专用） |
| `HoUserRawExit(exit_code)` | `SYS_RAW_EXIT` 封装（raw sentinel 专用） |

头文件末尾通过 `#undef` 将 `SYS_RAW_INVALID`、`SYS_RAW_WRITE`、`SYS_RAW_EXIT` 等裸常量从用户编译单元中撤销，阻止普通用户程序直接使用 raw syscall 号。

### 3.4 `user_counter` 迁移至稳定包装层（`1caa5bf`）

`src/user/user_counter/main.c` 完全移除对 bootstrap 魔法常量的直接依赖，改为：

```c
HoUserWaitForP1Gate();
if (!HoUserCurrentCapabilitySeedBlockIsValid())
    HoUserAbort();

HoUserWriteStdout(line, len);
HoUserExit(0);
```

Seed 校验、stdout handle 解析、write 调度，以及 `SYS_EXIT` 终止，全部通过 `libsys.h` 公开 wrapper 完成，源码中不再出现裸常量。

### 3.5 `user_hello` 保留为 raw regression sentinel（`1caa5bf`）

`src/user/user_hello/main.c` 继续承担 raw 证据链锚点职责，但所有 raw 访问均通过命名 helper 进行：

```c
status = HoUserRawProbeGuardPageByte();
status = HoUserRawWrite(msg, len);
HoUserRawExit(0);
```

`user_hello` 源码中不再散落 `KE_USER_BOOTSTRAP_STACK_GUARD_BASE` 固定地址；其 raw sentinel 角色通过 helper 接口维持，且在 spec 与注释中被明确标注为 raw/bootstrap-only，而非正式 ABI 示例。

---

## 4. 验证结果

### 4.1 P4.1 阶段（commit `819d1c9`）

**构建：** 执行全量构建，通过。

**回归测试（`test-user_hello` via `scripts/qemu_capture.sh`）：**

以下日志锚点均按顺序出现并被验证：

| 锚点 | 含义 |
|------|------|
| `[USERBOOT] invalid raw write rejected` | guard page 探测命中拒绝路径 |
| `[USERBOOT] hello write succeeds` | raw write 正向路径通过 |
| `[USERBOOT] SYS_RAW_EXIT` | raw exit 路径触发 |
| 线程终止标记 | 内核侧线程退出确认 |
| `[USERBOOT] bootstrap teardown complete` | teardown 全程完成 |

该轮验证表明：在正式 `SYS_EXIT` 与 `libsys.h` 重构引入后，`user_hello` 的 raw 证据链未受影响，现有回归覆盖完整保留。

**审查结论：** 非阻断（non-blocking）。

### 4.2 P4.2 阶段（commit `1caa5bf`）

**构建：**

1. `make clean && bear -- make all BUILD_FLAVOR=test-user_hello HO_DEMO_TEST_NAME=user_hello HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO`
2. `make clean && bear -- make all BUILD_FLAVOR=test-user_caps HO_DEMO_TEST_NAME=user_caps HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_CAPS`
3. `make clean && bear -- make all BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL`

**回归测试（`test-user_hello`、`test-user_caps`、`test-user_dual` — host 与 tcg 双模式）：**

| 测试 profile | 关键观测锚点 |
|-------------|-------------|
| `test-user_hello` | `SYS_RAW_EXIT`；teardown complete |
| `test-user_caps` | stdout capability write succeeds；`SYS_CLOSE` succeeded；stale capability rejection；`SYS_WAIT_ONE` succeeded；`SYS_RAW_EXIT`；teardown complete |
| `test-user_dual` (host) | `[USERCOUNTER] count=0/1/2`；`SYS_EXIT` for user_counter；`SYS_RAW_EXIT` for user_hello；双进程 teardown complete |
| `test-user_dual` (tcg) | 同上，tcg 模式下也完整出现 `count=0/1/2`、`SYS_EXIT`、`SYS_RAW_EXIT` 与双 teardown markers |

`user_dual` 在 host 与 tcg 两种执行模型上均完成双进程并发运行与退出，`SYS_EXIT`（正式路径，user_counter）与 `SYS_RAW_EXIT`（raw sentinel 路径，user_hello）同时在同一运行中被观测到，正面验证了稳定 ABI 与 raw sentinel 共存的设计预期。

**审查结论：** 非阻断（non-blocking）。

> **注意：** `scripts/qemu_capture.sh` 的 watchdog 超时属预期行为；验收结论基于日志锚点，而不是退出码本身。

---

## 5. 审查结论

两个提交阶段均经过 reviewer agent 评审，结论均为 **non-blocking**。无阻断性问题被记录，变更已按串行 gate 完成。

OpenSpec 工件（`proposal.md`、`design.md`、`specs/ex-facing-userspace-abi/spec.md`、`tasks.md`）在实施前已通过 `openspec validate` 校验，全部 tasks 均在执行结束前标记为完成。

> **说明：** `openspec/changes/stabilize-ex-facing-userspace-abi/tasks.md` 的 task 状态在执行过程中本地更新，但当前仓库状态下该文件未被 git 追踪到本次提交；任务完成状态以本日志为准。

---

## 6. 备注

- **`user_hello` 的角色约定已固化：** `user_hello` 是 raw/bootstrap 回归锚点，不是正式 ABI 示例。后续任何触及 `user_hello` 的改动应保持该角色。
- **raw lane 的使用范围：** `SYS_RAW_WRITE`、`SYS_RAW_EXIT` 及其 helper 被从公开命名空间中撤销；普通编译型用户程序不应再出现 `SYS_RAW_*` 裸常量。
- **`SYS_EXIT` 的 reserved 参数：** 调用方必须传 0；内核分发层已做显式零值校验。
- **后续范围边界：** 独立 UAPI 目录、`GETPID`/`GETTID` 等额外 syscall、`user_caps` 迁移至编译型用户程序，均明确不在本次变更范围内。
