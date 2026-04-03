agent 正在使用 change：`stabilize-user-bootstrap-p2-raw-syscalls`，工作流采用 `openspec apply` 的串行实施方式。

agent 正在确认上下文：该 change 为 `spec-driven`，共 8 个未完成任务，当前上下文已经覆盖 proposal、design、specs 与 tasks。

agent 正在拆分阶段计划：
1. 阶段一：先完成 `user_hello` payload 的 P2 自检序列，让同一个 profile 在 P1 gate 之后先走失败的 `SYS_RAW_WRITE` 探针，再走成功写入与 `SYS_RAW_EXIT`。
2. 阶段二：收敛 `user_bootstrap_syscall` 的 helper 命名、职责与诊断锚点，保持 `int 0x80` trap 入口继续是同步陷入的薄入口。
3. 阶段三：更新 `Readme.md`、`docs/current-ability.md` 和相关注释，明确 `user_hello` 仍是 bootstrap-only 的最小切片，不引入新的 P2-only profile，也不承诺未来正式 syscall ABI。
4. 阶段四：执行 `make clean`、指定 flavor 的构建与 `scripts/qemu_capture.sh`，核对完整证据链。

agent 正在按约定串行推进：每一阶段都先拉起 impl 实现，再拉起 reviewer 只审查 impl 修改过的部分；若 reviewer 给出阻塞，先尝试修复，再次送审；若仍然阻塞，则立即停止实施并在本日志记录原因。

agent 正在开始阶段一，目标是以最小修改完成 payload 自检，不扩大 profile 范围，不引入冗余抽象。

agent 正在执行阶段一实现：当前把修改收束在 `src/kernel/demo/user_hello.c`，让 payload 在 P1 gate 后先访问 stack guard page 做一次故意失败的 `SYS_RAW_WRITE` 探针，再比较返回值是否等于 `-EC_ILLEGAL_ARGUMENT`，随后执行成功的 hello-path `SYS_RAW_WRITE` 并比较返回字节数，最后才进入 `SYS_RAW_EXIT`。

agent 正在完成阶段一 reviewer 审查：本轮只检查 `user_hello.c` 的新增机器码序列与跳转终止逻辑，没有扩散审查其它模块。当前 reviewer 结论为“无阻塞”，已确认 guard page 探针会稳定落到拒绝路径、`-EC_ILLEGAL_ARGUMENT` 与成功字节数比较都和现有 raw syscall 编码一致、两处 `jne` 都会跳到统一的 `ud2` 终止陷阱。

agent 正在收尾阶段一：准备同步勾选 OpenSpec 的 `1.1` 与 `1.2`，随后提交本阶段 commit，再进入阶段二。
