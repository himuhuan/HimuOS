## 实施记录

### 任务目标

将 `docs/todo.md` 的 `P0` 收口为 OpenSpec change `demo-shell-scope-freeze`，并把这条 `demo-shell vertical slice` 口径固定下来，避免后续把范围误读成完整用户子系统或通用 shell 起步。

### 串行阶段

1. 生成 `proposal.md`、`design.md`、`spec.md`、`tasks.md`，把 `P0` 冻结为范围合同。
2. 对齐 `docs/todo.md`、`Readme.md`、`docs/handoff.md`，把公开叙事收束到同一条 demo-shell 口径。
3. 复核边界并写入一致性锚点，确保 `hsh`、命令面、编译型嵌入路径、PID 口径和 `P0`/`P1-P4` 分界都保持一致。

### Reviewer 结果

- 阶段 1：通过。
- 阶段 2：先阻塞，后修复，再通过。
- 阶段 3：通过。

### 阻塞与修复

阶段 2 的阻塞点出现在 `docs/todo.md`：文中仍依赖 `docs/handoff.md` 里旧的“单优先级 RR”前提，但 `docs/handoff.md` 已经被同步收紧为优先级感知 ready queue / RR 语义。修复方式是把 `docs/todo.md` 的相关表述改为直接基于当前代码现状描述，不再引用那条旧前提。

### 最终产物

- OpenSpec change `demo-shell-scope-freeze` 已完成。
- `Readme.md` 与 `docs/handoff.md` 已完成口径对齐。
- `tasks.md` 已全部勾选完成。
- `design.md` 中的 `Final Consistency Anchor` 已作为后续串行实施的范围锚点。

### 版本控制说明

OpenSpec 文件位于被 `.gitignore` 排除的 `openspec/` 路径下，不纳入版本控制。`docs/todo.md` 当前仍是未跟踪文件。本次预期纳入版本控制的是 `Readme.md`、`docs/handoff.md` 和 `agent-log.md`。
