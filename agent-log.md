# HimuOS MVP P2 实现报告

**变更标识**：`p2-dual-user-process-bringup`
**完成时间**：2026-04-05
**状态**：全部任务已完成 ✅

---

## 概述

本次工作基于 `docs/draft/mvp.md` P2 阶段目标，以 OpenSpec 提案的形式组织，由 impl / reviewer / writer 三类 Agent 串行协作完成。目标是在 HimuOS 内核中引入对多个并发用户态引导进程的支持，并通过一个双进程演示剖面验证端到端路径。

P2 在 P1（单用户 `user_hello` 已落地）的基础上推进三项工作：

1. 将 ex 层的单例引导运行时别名升级为支持多线程的注册表；
2. 新增第二个编译型用户态程序（`user_counter`）及其构建/嵌入链路；
3. 新增 `user_dual` 演示剖面，同时启动两个用户态线程并验证双路引导完整流程。

---

## OpenSpec 变更档案

变更名称：`p2-dual-user-process-bringup`

产物目录：`openspec/changes/p2-dual-user-process-bringup/`

| 文件 | 内容 |
|------|------|
| `proposal.md` | 需求背景与目标描述 |
| `design.md` | 架构决策与实现设计 |
| `specs/compiled-userspace-artifacts/spec.md` | 编译型用户态制品规格 |
| `specs/dual-user-process-demo/spec.md` | 双进程演示规格 |
| `tasks.md` | 阶段任务清单（全部已勾选） |

> 注：`openspec/` 目录已在 `.gitignore` 中排除，OpenSpec 文件不随代码提交入库，均为本次 Session 内带外维护的规划文档。

---

## 分阶段实现记录

### Phase 1 — Bootstrap Runtime Registry

**提交**：`69f657e` — `ex: support multiple bootstrap runtime aliases`

**背景**：原有实现在 `ex_bootstrap.c` 中以单例指针持有当前引导线程的运行时别名，仅支持一个线程的生命周期。若两个引导线程并发运行，第二个线程会覆盖第一个的别名，导致 teardown 时释放错误对象。

**主要变更**（3 个文件，+231 / -41 行）：

- `src/kernel/ex/ex_bootstrap.c`：将单例替换为以 `KTHREAD *` 为键的小型注册表，并用 `KE_CRITICAL_SECTION` 序列化所有注册表访问，同时修正 teardown 路径上的 alias unpublish 顺序；
- `src/kernel/ex/ex_bootstrap_internal.h`：更新内部接口声明；
- `src/kernel/ex/ex_bootstrap_adapter.c`：适配新注册表查找路径，保持 teardown 顺序（别名 unpublish 先于 `KTHREAD` 销毁）。

**reviewer 关注点**：teardown 顺序正确性及临界区覆盖完整性，经审核无遗留 blocker。

---

### Phase 2 — Compiled Userspace Artifacts

**提交**：`ab7a807` — `build: add compiled user_counter artifacts`

**背景**：P1 仅嵌入了 `user_hello` 一个用户态二进制。P2 需要第二个程序以便在双进程剖面中展示差异化行为。

**主要变更**（7 个文件，+168 / -8 行）：

- `src/user/user_counter/main.c`：新增 `user_counter` 用户态程序，循环输出 `[USERCOUNTER] count=N`（N = 0,1,2）后退出；
- `src/user/libsys.h`：抽取最小 `libsys` 能力 stdout 辅助宏，供两个用户态程序共用；
- `src/kernel/demo/user_counter_artifact_bridge.c`：为 `user_counter` 生成与 `user_hello` 对称的内核侧制品桥接；
- `src/kernel/demo/demo_internal.h`：将制品描述结构泛化，支持任意已嵌入二进制的统一描述；
- `makefile`：扩展构建规则，新增 `user_counter` 的编译、objcopy 内嵌及 depfile 生成；同时修复 `user_counter` 缺少 depfile include 的漏洞（reviewer 阶段发现的 blocker）。

---

### Phase 3 — Dual-Process Demo Profile

**提交**：`fe179e9` — `demo: add dual-user bootstrap profile`

**背景**：两个底层能力就绪后，需要一个演示剖面将它们串联：同时启动 `user_hello` 和 `user_counter`，验证双路引导、并发运行和独立 teardown 全链路。

**主要变更**（4 个文件，+114 / -5 行）：

- `src/kernel/demo/user_dual.c`：新增 `user_dual` 演示实现，依次创建并启动两个引导线程（分别加载 `user_hello` 和 `user_counter` 制品），并交由现有 scheduler / idle reaper 完成后续回收；
- `src/kernel/demo/demo.c`：注册 `HO_DEMO_TEST_USER_DUAL` 条件分支；
- `src/kernel/demo/demo_internal.h`：声明新剖面入口；
- `makefile`：新增 `BUILD_FLAVOR=test-user_dual` 构建目标。

---

## 验证结果

### 单用户回归测试（user_hello）

构建命令：
```
make clean && bear -- make all \
  BUILD_FLAVOR=test-user_hello \
  HO_DEMO_TEST_NAME=user_hello \
  HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO
```

运行命令：
```
BUILD_FLAVOR=test-user_hello \
HO_DEMO_TEST_NAME=user_hello \
HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO \
bash scripts/qemu_capture.sh 20 /tmp/himuos-final-user-hello.log
```

关键日志锚点（全部命中）：

| 锚点 | 说明 |
|------|------|
| `[DEMO] Selected profile: user_hello` | 剖面正确选中 |
| `[USERBOOT] enter user mode` | 进入用户态成功 |
| `[USERBOOT] hello` | 用户程序正常输出 |
| `[USERBOOT] bootstrap teardown complete code=0 thread=1` | 正常退出，code=0 |
| `[USERBOOT] idle/reaper reclaimed user_hello thread thread=1` | 线程被 reaper 回收 |

### 双进程新剖面测试（user_dual）

构建命令：
```
make clean && bear -- make all \
  BUILD_FLAVOR=test-user_dual \
  HO_DEMO_TEST_NAME=user_dual \
  HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL
```

运行命令：
```
BUILD_FLAVOR=test-user_dual \
HO_DEMO_TEST_NAME=user_dual \
HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL \
bash scripts/qemu_capture.sh 25 /tmp/himuos-final-user-dual.log
```

关键日志锚点（全部命中）：

| 锚点 | 说明 |
|------|------|
| `[DEMO] Selected profile: user_dual` | 剖面正确选中 |
| `Thread 1 created` + `Thread 2 created` | 两个引导线程均已创建 |
| 两条 `[USERBOOT] enter user mode` | 两路独立进入用户态 |
| `[USERCOUNTER] count=0/1/2` | user_counter 程序正常运行 |
| `[USERBOOT] hello` | user_hello 程序正常输出 |
| 两条 `[USERBOOT] bootstrap teardown complete ...` | 两路均正常退出 |
| 两条 idle/reaper reclaim 行 | 两路线程均被正确回收 |

---

## 提交汇总

| 提交 SHA | 信息 | 阶段 |
|----------|------|------|
| `69f657e` | `ex: support multiple bootstrap runtime aliases` | Phase 1 |
| `ab7a807` | `build: add compiled user_counter artifacts` | Phase 2 |
| `fe179e9` | `demo: add dual-user bootstrap profile` | Phase 3 |

上述三个实现提交均已在本地 `main` 生成；是否推送到远端需按仓库工作流另行处理。

---

## 遗留说明

- **OpenSpec 文件**：`openspec/changes/p2-dual-user-process-bringup/` 下的所有规划文档已在本次 Session 内维护完毕，但因该目录被 `.gitignore` 排除，不会随代码提交入库。如需存档，需手动处理。
- **推送**：本次三个提交均仅在本地 `main`，尚待推送或提 PR。
- **worktree 脏文件**：`docs/draft/mvp.md` 在工作区中仍有用户侧未暂存修改；本文件（`agent-log.md`）为本次 closeout 生成结果，未计入上述三次实现阶段提交统计。
- **后续方向**：P2 目标已全部达成。P3 及后续阶段可参考 `docs/draft/mvp.md` 中的规划继续推进。
