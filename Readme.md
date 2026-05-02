# Himu Operating System (HimuOS)

*A UEFI-based x86_64 Macro Kernel with an Executive Lite user runtime*

## 概要
本项目旨在从零开始设计并实现一个名为 "HimuOS" 的x86_64架构宏内核操作系统。

与传统的依赖Legacy BIOS的教学系统不同，本项目将专注于现代PC的UEFI (统一可扩展固件接口) 引导标准。系统将通过一个自定义的UEFI引导管理器 (HimuBootManager) 加载，该管理器负责初始化图形输出、获取内存映射，并最终将执行权交给内核。

在内核架构层面，HimuOS 借鉴 Windows NT 的分层宏内核设计思想，将内核划分为机制层（Ke 层）与策略层（Ex 层）两个层次。Ke 层负责硬件状态的原子化抽象与维护，包括中断描述符表（Interrupt Descriptor Table，IDT）管理、时间源（Time Source）与时钟事件（Clock Event）等底层机制的封装；Ex 层在 Ke 层之上持有用户态可见的进程、线程、句柄与系统调用 contract。两层之间遵循严格的单向依赖原则，即 Ex 层依赖 Ke 层而非反向调用，从而有效降低模块间耦合度；从项目对外口径上，**用户态只能通过 Ex 暴露的接口访问内核服务**。

在 Ke 层的具体实现中，本文采用设备—汇（Device-Sink）抽象模式，为控制台、时间源与时钟事件等子系统提供统一的硬件抽象接口，支持多种底层驱动实现（如时间戳计数器 TSC、高精度事件定时器 HPET、本地高级可编程中断控制器定时器 LAPIC Timer 等）的透明切换。系统建立了基于四级页表的虚拟内存管理机制，实现了内核态（Ring 0）与用户态（Ring 3）的特权级隔离，并通过共享 `int 0x80` trap entry + Ex-facing syscall contract 为用户态提供最小服务入口。

当前 README 描述的是已经落地的 **Executive Lite user runtime** 主线：内核启动后由 Ex 装载用户态程序，维护进程、线程、对象、句柄、系统调用、前台输入与 sysinfo 合同。默认交互入口是 `demo_shell` profile，它拉起用户态 `hsh`，并围绕 `sysinfo` / `memmap` / `ps` / `calc` / `tick1s` / `kill` / `exit` 完成一条可演示、可复述、可稳定回归的官方路径。

当前 runtime 具备以下能力：

- 虚拟内存管理：建立并启用四级页表，为内核和每个用户进程提供隔离的地址空间。
- 特权级分离：实现内核态（Ring 0）和用户态（Ring 3）的安全隔离。
- 用户程序模型：以**编译型 C 用户程序**作为正式用户程序形态，`hsh`、`calc`、`tick1s`、`fault_de`、`fault_pf`、`user_counter`、`user_hello`、`user_caps`、`input_probe` 与 `line_echo` 均通过嵌入内核的 Ex runtime 路径装载。
- 系统调用与句柄：以 Ex-facing 的最小句柄化 syscall contract 作为用户态请求服务的正式方向，当前覆盖 stdout、readline、spawn、wait、kill、sysinfo、sleep、close 与 exit。
- 并发与调度：在单处理器（AP）上以抢占式调度支撑这条 demo-shell 切片；当前调度器已经具备优先级感知 ready queue 与 RR 时间片语义，因此后续主线不再把“先补优先级调度”当作前置阶段。
- 可观测性：以 GOP 文本输出和 COM1 串口输出作为主要演示与诊断界面。

> [!IMPORTANT]
> 当前 runtime **不包含** 文件系统、多核（SMP）、PATH 搜索、通用 ELF / runtime loader、内核态 shell、POSIX job control 与完整 Object Manager。`hsh` 是受限 demo shell，而不是通用 shell ABI。

## HimuOS 参数说明

| 参数     | 说明                                        |
| ------ | ----------------------------------------- |
| 体系架构   | x86_64 (AMD64) 兼容微机或虚拟机                   |
| 内核架构   | 宏内核                                       |
| 引导     | 仅在支持统一可扩展固件接口(UEFI) 的微机上 使用 软驱或者光驱启动      |
| 启动方式   | 自定义UEFI Loader HimuBootManager + HBM 启动协议 |
| 内存模型   | 平坦地址模型；x64 长模式；48 位可寻址空间                  |
| 存储器    | 四级页表；页式虚拟存储器                              |
| 物理内存   | 最低 32MB, 最高 128GB                         |
| 特权级    | 支持内核态（Ring 0) 和用户态 (Ring 3)               |
| 用户空间   | 每进程私有地址空间；当前以固定 user image window 装载用户映像   |
| 系统调用   | 目标方向为 `int 0x80` + Ex-facing 最小句柄化 syscall contract |
| 并发与同步  | 仅支持 单AP；当前调度器为优先级感知的抢占式 RR / tickless 语义        |
| 多线程    | 支持内核级线程调度                                 |
| 动态内存分配 | 支持                                        |
| 中断     | 支持中断                                      |
| 文件系统   | 不支持                                       |
| 显示器    | 支持 GOP 彩色文本界面与 COM1 串口输出                  |
| 设备支持   | GOP / COM1 / PS/2 键盘 / MMIO 支持 |

## 回归 profile 与推荐执行流程

HimuOS 当前把内核 demo/test profile 视为稳定的 regression profile。推荐流程始终是：

```bash
make clean
bear -- make all BUILD_FLAVOR=<flavor> HO_DEMO_TEST_NAME=<profile> HO_DEMO_TEST_DEFINE=<define>

BUILD_FLAVOR=<flavor> HO_DEMO_TEST_NAME=<profile> HO_DEMO_TEST_DEFINE=<define> \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-<profile>.log
```

其中 `scripts/qemu_capture.sh` 是主运行与串口捕获入口；不要把隐式 `make run` 或 `make test` 当作默认验证路径。脚本默认使用 `QEMU_CAPTURE_MODE=host`（host/KVM），也支持显式选择 `QEMU_CAPTURE_MODE=tcg` 或 `QEMU_CAPTURE_MODE=custom`。

无参 `make run`、`make iso`、`make run_iso` 现在默认装配 `demo_shell` 作为交互入口，并使用独立的 `default-demo_shell` build flavor，避免与无 profile 的普通构建产物混用。该入口的键盘输入来自运行时 PS/2 键盘链路；手工交互时应把焦点切到 QEMU 的 GTK 窗口，而不是在承载 `-serial stdio` 的宿主终端里输入。

为避免 GTK 交互期间被运行时日志持续刷屏，`make run`、`make iso`、`make run_iso` 在 `QEMU_DISPLAY=gtk` 下默认把内核最小日志等级提升到 `WARNING`；因此 `DBG` 和 `INF` 都不会进入 GTK 交互输出。无头 `qemu_capture.sh` 路径默认仍保留 `DBG`，以便继续做时序与故障诊断。

如需把内核图形控制台切为白底黑字，可传入 `HO_ENABLE_CONSOLE_LIGHT_THEME=1`。该开关只影响内核接管 GOP 之后的图形控制台默认主题与首次清屏，不影响 UEFI 文本阶段，也不改变 COM1 串口捕获内容。例如：`make run HO_ENABLE_CONSOLE_LIGHT_THEME=1`，或 `HO_ENABLE_CONSOLE_LIGHT_THEME=1 bash scripts/qemu_capture.sh 30 /tmp/himuos-demo.log`。

> [!IMPORTANT]
> 对 `user_dual` 以及其他时序敏感 / 销毁敏感 profile，**单份 host/KVM 捕获不再视为充分证据**。回归结论必须同时给出 host 与 TCG 两份日志；若两条执行模型结果不一致，必须在缺陷记录中明确注明。

### 稳定 profile 标识符

| Profile | Build flavor | Define | Outcome class | Intent |
| ------ | ------ | ------ | ------ | ------ |
| `schedule` | `test-schedule` | `HO_DEMO_TEST_SCHEDULE` | clean pass with continued boot/idle | scheduler smoke coverage, thread/event/semaphore/mutex 基线路径 |
| `demo_shell` | `test-demo_shell` | `HO_DEMO_TEST_DEMO_SHELL` | official timing-sensitive contract | `hsh` interactive vertical slice, sysinfo/memmap/ps, foreground `calc`, background `tick1s`, kill, clean shell exit |
| `user_input` | `test-user_input` | `HO_DEMO_TEST_USER_INPUT` | official timing-sensitive contract | `input_probe` → `line_echo` foreground handoff, readline ownership, teardown, foreground owner reset |
| `user_dual` | `test-user_dual` | `HO_DEMO_TEST_USER_DUAL` | official timing-sensitive contract | concurrent formal-ABI `user_hello` / `user_counter`, process wait, teardown and reaper evidence |
| `user_fault` | `test-user_fault` | `HO_DEMO_TEST_USER_FAULT` | official timing-sensitive contract | child `#DE` / `#PF` isolation, foreground restore, recovery to `hsh` |
| `user_hello` | `test-user_hello` | `HO_DEMO_TEST_USER_HELLO` | formal ABI smoke profile | 由 `src/user/user_hello` 源码编译并接入 kernel 的最小 Ring 3 进入、formal `SYS_WRITE` guard-page rejection、stdout hello write、`SYS_EXIT`、thread-terminated → finalizer teardown → idle/reaper reclaimed 证据链 |
| `user_caps` | `test-user_caps` | `HO_DEMO_TEST_USER_CAPS` | formal capability/wait regression | 版本化 capability seed block、stdout capability write、`SYS_CLOSE`、stale-handle rejection、`SYS_WAIT_ONE` 与 `SYS_EXIT` 证据链 |
| `guard_wait` | `test-guard_wait` | `HO_DEMO_TEST_GUARD_WAIT` | diagnosable contract violation or panic | critical-section guard misuse |
| `owned_exit` | `test-owned_exit` | `HO_DEMO_TEST_OWNED_EXIT` | diagnosable contract violation or panic | exit while owning a mutex |
| `irql_wait` | `test-irql_wait` | `HO_DEMO_TEST_IRQL_WAIT` | diagnosable contract violation or panic | wait at `DISPATCH_LEVEL` |
| `irql_sleep` | `test-irql_sleep` | `HO_DEMO_TEST_IRQL_SLEEP` | diagnosable contract violation or panic | sleep at `DISPATCH_LEVEL` |
| `irql_yield` | `test-irql_yield` | `HO_DEMO_TEST_IRQL_YIELD` | diagnosable contract violation or panic | yield at `DISPATCH_LEVEL` |
| `irql_exit` | `test-irql_exit` | `HO_DEMO_TEST_IRQL_EXIT` | diagnosable contract violation or panic | thread exit at `DISPATCH_LEVEL` |
| `pf_imported` | `test-pf_imported` | `HO_DEMO_TEST_PF_IMPORTED` | intentional fatal page-fault halt with bounded diagnostics | imported kernel-data NX fault diagnosis |
| `pf_guard` | `test-pf_guard` | `HO_DEMO_TEST_PF_GUARD` | intentional fatal page-fault halt with bounded diagnostics | stack guard-page diagnosis |
| `pf_fixmap` | `test-pf_fixmap` | `HO_DEMO_TEST_PF_FIXMAP` | intentional fatal page-fault halt with bounded diagnostics | active fixmap alias diagnosis |
| `pf_heap` | `test-pf_heap` | `HO_DEMO_TEST_PF_HEAP` | intentional fatal page-fault halt with bounded diagnostics | heap-backed KVA diagnosis |

当前用户态相关的官方安全网是 `demo_shell`、`user_input`、`user_dual` 与 `user_fault`，这四个 profile 涉及交互、前台输入、并发或 teardown，结论必须同时给出 host 与 TCG 捕获证据。`user_hello` 是最小 formal ABI smoke profile，固定 `src/user/libsys.h`、stdout write、invalid-buffer rejection 与 clean exit 证据链；`user_caps` 是 formal capability / handle 回归项，固定 capability seed、handle close、stale-handle rejection、wait timeout 与 clean exit 证据链。

`user_dual` 则应被视为**正式 ABI 双进程运行时的时序敏感回归项**：当它涉及 teardown / preemption 相关结论时，必须同时检查 `QEMU_CAPTURE_MODE=host` 与 `QEMU_CAPTURE_MODE=tcg` 两条路径。

### 例子

```bash
# clean-pass profile
make clean
bear -- make all BUILD_FLAVOR=test-schedule HO_DEMO_TEST_NAME=schedule HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_SCHEDULE
BUILD_FLAVOR=test-schedule HO_DEMO_TEST_NAME=schedule HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_SCHEDULE \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-schedule.log

# compiled userspace hello profile
make clean
bear -- make all BUILD_FLAVOR=test-user_hello HO_DEMO_TEST_NAME=user_hello HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO
BUILD_FLAVOR=test-user_hello HO_DEMO_TEST_NAME=user_hello HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-user-hello.log

# formal capability/wait regression
make clean
bear -- make all BUILD_FLAVOR=test-user_caps HO_DEMO_TEST_NAME=user_caps HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_CAPS
BUILD_FLAVOR=test-user_caps HO_DEMO_TEST_NAME=user_caps HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_CAPS \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-user-caps.log

# formal-ABI dual userspace runtime (timing-sensitive: collect both execution models)
make clean
bear -- make all BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL
BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL \
    QEMU_CAPTURE_MODE=host bash scripts/qemu_capture.sh 30 /tmp/himuos-user-dual-host.log
BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL \
    QEMU_CAPTURE_MODE=tcg bash scripts/qemu_capture.sh 30 /tmp/himuos-user-dual-tcg.log
```

## 版权与许可

本项目遵循 GNU 通用公共许可证 (GPL) 第 3 版或更高版本发布。您可以自由地使用、修改和分发本项目的代码。

本项目适用于本科及以上计算机科学与技术相关专业的教学与实验；

你可以自由使用本项目的代码，但禁止将本项目的代码用于商业用途。
