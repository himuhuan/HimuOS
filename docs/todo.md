**核心判断**
你这次要的，不应该再按“做一个完整 OS”来拆，而应该按“把已经成立的 MVP，收口成一个看起来就是完整操作系统的演示切片”来拆。

现状上，仓库其实已经有了很强的地基：双编译型用户程序同时启动已经存在，[user_dual.c](/home/liuhuan/HimuOS/src/kernel/demo/user_dual.c#L37)；用户态也已经有稳定的 `int 0x80` 包装和 `SYS_WRITE` / `SYS_EXIT` 路径，[libsys.h](/home/liuhuan/HimuOS/src/user/libsys.h#L26)。所以新计划不该再重走 `docs/mvp.md` 早期那套“先把用户态立起来”的路线，而应直接做一个 **demo-shell vertical slice**。

```text
Boot
  -> Kernel init
    -> create hsh (user-mode)
      -> "& tick1s"  -> background user process, print PID
      -> "calc"      -> foreground user process, interactive RPN
      -> while calc runs, tick1s keeps printing TICK!
      -> "kill <pid>" -> kernel reports kill, process exits
      -> demo end
```

一个需要提前对齐的事实是：调度器代码里已经有多级 ready queue 和最高优先级选取逻辑，[scheduler_internal.h](/home/liuhuan/HimuOS/src/kernel/ke/thread/scheduler/scheduler_internal.h#L29)。所以这次不要再把“优先级调度”列成主线阶段；真正不可绕开的硬前置，其实是 **内核键盘输入**。而当前仓库依然只有 bootloader 级 `ReadKeyStroke`，没有内核键盘驱动/输入缓冲，[handoff.md](/home/liuhuan/HimuOS/docs/handoff.md#L137)；bootloader 那条 `ConsoleReadline` 依赖 UEFI Boot Services，出了 `ExitBootServices` 后不能拿来充当运行期输入方案，[io.c](/home/liuhuan/HimuOS/src/boot/v2/io.c#L22)。

**分阶段计划**
- `P0：冻结演示口径，不再扩大战线`
  - 把目标正式定义为“shell 化 MVP 演示”，不是完整用户子系统。
  - 更新 文档，同步口径。
  - 明确 `hsh` 必须是编译型用户程序，由内核 boot 后直接拉起；不要做内核态 shell。
  - 明确 `tick1s`、`calc`、`hsh` 都继续走“编译产物嵌入内核 + ExBootstrapCreateProcess/CreateThread/StartThread”这条现有路径，不引入 FS、PATH、通用 loader。
  - 明确 PID 采用 MVP 口径：建议直接复用“进程唯一主线程的 `ThreadId` 作为演示 PID”，不要另起一套 PID allocator。
  - `P0` 完成标志：团队对“只做一个官方 demo profile，且命令只有 `tick1s` / `calc` / `kill` / `help` / `exit`”口径一致。

- `P1：补最小输入闭环，这是唯一硬前置`
  - 做一条极薄的内核输入链路：键盘中断/扫描码翻译/ASCII 过滤/行缓冲。
  - 只支持 MVP 必要键：可打印字符、`Enter`、`Backspace`。不做历史、补全、组合键、编辑器式光标移动。
  - 输入模型不要一般化成完整 TTY；只做“单前台消费者”语义。谁是前台，谁才能读键盘。
  - 这一步产出的不是“通用 stdin 子系统”，而是“hsh/calc 能读一行文本”的最小 contract。
  - `P1` 完成标志：内核进入后，前台程序能稳定收到一行输入；前台 ownership 可在 `hsh` 与 `calc` 之间切换。

- `P2：建立 demo-scope 的进程控制面`
  - `hsh` 作为官方入口启动，启动后出现提示符。
  - 内核新增的控制面必须保持很窄，建议只补这几个能力：`readline`、`spawn_builtin(program_id, flags)`、`wait_pid(pid)`、`kill_pid(pid)`、`sleep_ms(ms)`。
  - 不建议这轮补 `getpid`；因为 shell 只需要在 `spawn` 返回值里拿到 PID 即可。
  - `spawn_builtin` 不要吃字符串，避免把命令解析搬进内核。字符串解析留在 `hsh` 用户态，内核只接收一个很小的程序枚举值。
  - `hsh` 内部只维护一个很小的 job 表：`pid / name / foreground-background / alive`。
  - `P2` 完成标志：输入 `& tick1s` 时，shell 立即回显“已启动，PID=...”，并返回提示符。

- `P3：完成三个用户程序的演示行为`
  - `hsh`：硬编码命令表，不做脚本、管道、重定向、环境变量、通配符。
  - `tick1s`：无限循环 `sleep_ms(1000)` + `write("TICK!\n")`。
  - `calc`：前台 RPN REPL，按行读取，支持最小集合即可，例如整数、`+ - * /`、`q` 退出。
  - 交互期间允许控制台输出交错，不需要做 prompt 重绘或“干净终端”体验。你要的只是“能看到 calc 交互时 tick1s 还在输出”，不是完整 tty discipline。
  - `P3` 完成标志：`calc` 运行时，`tick1s` 的 `TICK!` 仍会穿插出现在控制台；`calc` 退出后控制权回到 `hsh`。

- `P4：补 kill 语义并冻结演示`
  - `kill <pid>` 的 MVP 语义应非常保守：只支持杀死由 `hsh` 启动、且仍存活的用户进程。
  - 在 MVP 下可以把“进程”近似为“一个 `ExProcess` + 一个主 `ExThread`”；`kill` 实际针对主线程终止路径，复用现有成熟的 terminate/finalizer/reaper 链。
  - 内核必须打印一条清楚的 kill 成功消息，作为答辩和录屏锚点。
  - 这条新 profile 本质上也是 timing-sensitive 的多进程演示，因此验证流程必须沿用现有约束：`host` 和 `tcg` 两份 QEMU 捕获都要有。
  - `P4` 完成标志：固定一套官方演示脚本和日志锚点，能稳定复现“启动 hsh -> 启动 tick1s -> 进入 calc -> 退出 calc -> kill tick1s -> 结束”。

**几条建议，能明显减少过度工程化**
- 不做通用 shell，只做 `hsh` 这个演示壳。
- 不做文件系统，不做 PATH 搜索，不做 ELF loader，继续走嵌入式用户程序表。
- 不把 stdin/stdout/tty 一般化成完整对象模型；先只实现“单前台 reader + 共享 console writer”。
- 不做信号系统；`kill` 就是一个 demo-scope 的终止请求。
- 不做真正的多线程用户进程；这轮默认“一进程一主线程”，PID 直接复用主线程 ID。
- 不把现有 Ex/handle 模型重构成完整 Object Manager；这轮只在现有 contract 上补最小控制面。

**我建议的优先顺序**
`P0 -> P1 -> P2 -> P3 -> P4`

原因很简单：没有 `P1`，`hsh` 和 `calc` 都只能是假交互；没有 `P2`，你就只能继续做 `user_dual` 风格的“预排脚本演示”；而 `P3/P4` 才是在已有交互与进程控制面上把故事讲完整。

`P0` 已整理为 OpenSpec change `demo-shell-scope-freeze`。后续实施应严格按该 change 的 `proposal` / `design` / `tasks` 串行推进，并在 `P0` 收口前避免提前落入 `P1`-`P4` 的运行时实现。
