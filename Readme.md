# Himu Operating System (HimuOS)

*A UEFI-based x86_64 Macro Kernel with a Dual-Process User-Mode MVP*

## 概要
本项目旨在从零开始设计并实现一个名为 "HimuOS" 的x86_64架构宏内核操作系统。

与传统的依赖Legacy BIOS的教学系统不同，本项目将专注于现代PC的UEFI (统一可扩展固件接口) 引导标准。系统将通过一个自定义的UEFI引导管理器 (HimuBootManager) 加载，该管理器负责初始化图形输出、获取内存映射，并最终将执行权交给内核。

在内核架构层面，HimuOS 借鉴 Windows NT 的分层宏内核设计思想，将内核划分为机制层（Ke 层）与策略层（Ex 层）两个层次。Ke 层负责硬件状态的原子化抽象与维护，包括中断描述符表（Interrupt Descriptor Table，IDT）管理、时间源（Time Source）与时钟事件（Clock Event）等底层机制的封装；Ex 层在 Ke 层之上持有用户态可见的进程、线程、句柄与系统调用 contract。两层之间遵循严格的单向依赖原则，即 Ex 层依赖 Ke 层而非反向调用，从而有效降低模块间耦合度；从项目对外口径上，**用户态只能通过 Ex 暴露的接口访问内核服务**。

在 Ke 层的具体实现中，本文采用设备—汇（Device-Sink）抽象模式，为控制台、时间源与时钟事件等子系统提供统一的硬件抽象接口，支持多种底层驱动实现（如时间戳计数器 TSC、高精度事件定时器 HPET、本地高级可编程中断控制器定时器 LAPIC Timer 等）的透明切换。系统建立了基于四级页表的虚拟内存管理机制，实现了内核态（Ring 0）与用户态（Ring 3）的特权级隔离，并通过共享 `int 0x80` trap entry + Ex-facing syscall contract 为用户态提供最小服务入口。

当前 README 以本轮 **MVP 交付范围** 为准，而不再继续承诺完整用户子系统的最终形态。该 MVP 的目标是交付一个**正式成立、可论证、可演示的双进程用户态原型操作系统**。

本轮 MVP 以以下能力为交付目标：

- 虚拟内存管理：建立并启用四级页表，为内核和每个用户进程提供隔离的地址空间。
- 特权级分离：实现内核态（Ring 0）和用户态（Ring 3）的安全隔离。
- 用户程序模型：以**编译型 C 用户程序**作为正式用户程序形态，并将其装载到每个 `ExProcess` 的私有地址空间中。
- 系统调用与句柄：以 Ex-facing 的最小句柄化 syscall contract 作为用户态请求服务的正式方向，当前聚焦 stdout、wait、close、exit 等原型级能力。
- 并发与调度：在单处理器（AP）上以抢占式时间片轮转（RR）为基线支撑双进程原型；若团队时间允许，再补入简化版优先级调度。
- 可观测性：以 GOP 文本输出和 COM1 串口输出作为主要演示与诊断界面。

> [!IMPORTANT]
> 本轮 MVP **不包含** 文件系统、多核（SMP）、键盘输入、Shell、通用 ELF Loader 与完整 Object Manager。

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
| 用户空间   | 每进程私有地址空间；当前以固定 bootstrap window 装载用户映像   |
| 用户程序模型 | 目标为编译型 C 用户程序；当前 MVP 以双进程原型为交付目标         |
| 系统调用   | 目标方向为 `int 0x80` + Ex-facing 最小句柄化 syscall contract |
| 并发与同步  | 仅支持 单AP；当前下限为 RR，简化优先级调度为可选增强项            |
| 多线程    | 支持内核级线程调度                                 |
| 动态内存分配 | 支持                                        |
| 中断     | 支持中断                                      |
| 文件系统   | 不支持                                       |
| 显示器    | 支持 GOP 彩色文本界面与 COM1 串口输出                  |
| 设备支持   | GOP / COM1 / MMIO 支持；键盘与 Shell 不在本轮 MVP 范围 |

## 回归 profile 与推荐执行流程

HimuOS 当前把内核 demo/test profile 视为稳定的 regression profile。推荐流程始终是：

```bash
make clean
bear -- make all BUILD_FLAVOR=<flavor> HO_DEMO_TEST_NAME=<profile> HO_DEMO_TEST_DEFINE=<define>

BUILD_FLAVOR=<flavor> HO_DEMO_TEST_NAME=<profile> HO_DEMO_TEST_DEFINE=<define> \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-<profile>.log
```

其中 `scripts/qemu_capture.sh` 是主运行与串口捕获入口；不要把隐式 `make run` 或 `make test` 当作默认验证路径。

### 稳定 profile 标识符

| Profile | Build flavor | Define | Outcome class | Intent |
| ------ | ------ | ------ | ------ | ------ |
| `schedule` | `test-schedule` | `HO_DEMO_TEST_SCHEDULE` | clean pass with continued boot/idle | scheduler smoke coverage, thread/event/semaphore/mutex 基线路径 |
| `user_hello` | `test-user_hello` | `HO_DEMO_TEST_USER_HELLO` | compiled minimal userspace bring-up | 由 `src/user/user_hello` 源码编译并接入 kernel 的最小 Ring 3 进入、来自 CPL3 的 P1 timer round-trip、P1 gate 之后的 rejected raw write probe / successful hello write / `SYS_RAW_EXIT`、P3 teardown-complete → thread terminated → idle/reaper reclaimed 证据链 |
| `user_caps` | `test-user_caps` | `HO_DEMO_TEST_USER_CAPS` | bootstrap-only capability pilot | 版本化 capability seed block、stdout capability write、`SYS_CLOSE`、stale-handle rejection、`SYS_WAIT_ONE` 与 clean exit 证据链 |
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

当前用户态相关的稳定锚点主要是 `user_hello` 与 `user_caps`。前者已经固定为**由 `src/user/user_hello` 源码编译产生的用户程序**的最小 Ring 3 进入与 clean exit 证据链，后者固定 capability / handle 路径的最小合同。README 描述的 MVP 方向，是在这两条稳定锚点之上继续推进到**编译型、双进程、Ex-facing 的用户态原型**；因此，这两条 profile 应被视为当前主线的阶段性回归基础，而不是最终用户 ABI 的全部形态。

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

# bootstrap capability pilot
make clean
bear -- make all BUILD_FLAVOR=test-user_caps HO_DEMO_TEST_NAME=user_caps HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_CAPS
BUILD_FLAVOR=test-user_caps HO_DEMO_TEST_NAME=user_caps HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_CAPS \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-user-caps.log
```

## 版权与许可

本项目遵循 GNU 通用公共许可证 (GPL) 第 3 版或更高版本发布。您可以自由地使用、修改和分发本项目的代码。

本项目适用于本科及以上计算机科学与技术相关专业的教学与实验；

你可以自由使用本项目的代码，但禁止将本项目的代码用于商业用途。
