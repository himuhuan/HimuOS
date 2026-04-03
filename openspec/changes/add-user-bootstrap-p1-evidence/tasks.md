## 1. P1 Staging And Payload Gate

- [x] 1.1 在 `src/include/kernel/ke/user_bootstrap.h` 和 `src/kernel/ke/user_bootstrap.c` 中为 P1 增加 staging 状态、用户栈 mailbox 常量与稳定日志锚点，明确“first entry / timer hit count / gate armed”三类里程碑
- [x] 1.2 调整 `src/kernel/demo/user_hello.c` 的最小 payload，使其首次进入 Ring 3 后先等待 mailbox 放行，再继续现有 `SYS_RAW_WRITE` / `SYS_RAW_EXIT` 路径

## 2. Timer-Origin Phase-One Evidence

- [x] 2.1 在 timer 中断观测点或等价 trap 返回路径中，仅针对带有 bootstrap staging 的当前线程统计“来自 CPL3 的 timer 抢占”，并排除同步 `int 0x80` 或 CPL0 tick
- [x] 2.2 在第二次用户态 timer 抢占达成时 arm P1 gate、写入 mailbox sentinel，并输出稳定的 P1 证据日志，不改变现有 raw syscall dispatcher 语义

## 3. Profile Contract And Documentation

- [x] 3.1 更新 `Readme.md`、`docs/current-ability.md` 与相关代码注释，明确 `user_hello` 现在必须先证明 P1 的 timer round-trip，再进入 hello/write/exit 阶段
- [x] 3.2 复查 `user_hello` 的证据链顺序与命名，确保 profile 语义仍然是单一最小用户态闭环，而不是新增独立 P1-only profile

## 4. Validation

- [ ] 4.1 按显式 workflow 执行 `make clean`、`bear -- make all BUILD_FLAVOR=test-user_hello HO_DEMO_TEST_NAME=user_hello HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO`
- [ ] 4.2 使用 `scripts/qemu_capture.sh` 采集 `user_hello` 日志，并确认顺序覆盖“enter user mode -> timer from user #1 -> timer from user #2 / gate armed -> hello -> SYS_RAW_EXIT -> idle/reaper”
