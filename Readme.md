# Himu Operating System (HimuOS)

*Design and Implementation of a UEFI-based x86_64 User-Mode Operating System Kernel*

## 概要
本项目旨在从零开始设计并实现一个名为 "HimuOS" 的x86_64架构宏内核操作系统。

与传统的依赖Legacy BIOS的教学系统不同，本项目将专注于现代PC的UEFI (统一可扩展固件接口) 引导标准。系统将通过一个自定义的UEFI引导管理器 (HimuBootManager) 加载，该管理器负责初始化图形输出、获取内存映射，并最终将执行权交给内核。

在内核架构层面，HimuOS 借鉴 Windows NT 的分层宏内核设计思想，将内核划分为机制层（Ke 层）与策略层（Ex 层）两个层次。Ke 层负责硬件状态的原子化抽象与维护，包括中断描述符表（Interrupt Descriptor Table，IDT）管理、时间源（Time Source）与时钟事件（Clock Event）等底层机制的封装；Ex 层在 Ke 层之上引入对象管理器（Object Manager），将内核资源统一抽象为受控对象，并通过基于能力（Capability）的句柄模型实现用户态与内核态之间的安全隔离。两层之间遵循严格的单向依赖原则，即 Ex 层依赖 Ke 层而非反向调用，从而有效降低模块间耦合度。

在 Ke 层的具体实现中，本文采用设备—汇（Device-Sink）抽象模式，为控制台、时间源与时钟事件等子系统提供统一的硬件抽象接口，支持多种底层驱动实现（如时间戳计数器 TSC、高精度事件定时器 HPET、本地高级可编程中断控制器定时器 LAPIC Timer 等）的透明切换。系统建立了基于四级页表的虚拟内存管理机制，实现了内核态（Ring 0）与用户态（Ring 3）的特权级隔离，并设计了基于系统调用的统一服务入口。

内核将以x86_64长模式 (Long Mode) 运行，并实现现代操作系统的核心功能：

- 虚拟内存管理： 建立并启用四级页表，为内核和用户程序提供隔离的、平坦的虚拟地址空间。
- 特权级分离： 实现内核态 (Ring 0) 和用户态 (Ring 3) 的安全隔离。
- 系统调用： 设计并实现一个系统调用接口，作为用户态程序获取内核服务的唯一入口。
- 并发与调度： 在单处理器（AP）上实现一个基于优先级和时间片轮转（RR）的多任务调度器，支持内核级线程和上下文切换。
- 基础驱动： 支持基于GOP（UEFI）的彩色文本界面和键盘循环缓冲输入。

> [!IMPORTANT]
> 本项目**不包含**文件系统和多核（SMP）支持.

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
| 用户空间   | 约 16 EB - 128 TB 地址空间                     |
| 并发与同步  | 支持；仅支持 单AP 下的多任务调度                        |
| 多线程    | 支持内核级线程调度                                 |
| 动态内存分配 | 支持                                        |
| 中断     | 支持中断                                      |
| 文件系统   | 不支持                                       |
| 显示器    | 支持彩色的文本界面                                 |
| 设备支持   | 标准 VGA 显示器、标准QWERTY键盘输入支持；MMIO 支持         |

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

### 例子

```bash
# clean-pass profile
make clean
bear -- make all BUILD_FLAVOR=test-schedule HO_DEMO_TEST_NAME=schedule HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_SCHEDULE
BUILD_FLAVOR=test-schedule HO_DEMO_TEST_NAME=schedule HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_SCHEDULE \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-schedule.log

# guard-misuse profile
make clean
bear -- make all BUILD_FLAVOR=test-guard_wait HO_DEMO_TEST_NAME=guard_wait HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_GUARD_WAIT
BUILD_FLAVOR=test-guard_wait HO_DEMO_TEST_NAME=guard_wait HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_GUARD_WAIT \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-guard-wait.log

# page-fault profile
make clean
bear -- make all BUILD_FLAVOR=test-pf_guard HO_DEMO_TEST_NAME=pf_guard HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_PF_GUARD
BUILD_FLAVOR=test-pf_guard HO_DEMO_TEST_NAME=pf_guard HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_PF_GUARD \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-pf-guard.log
```

## 版权与许可

本项目遵循 GNU 通用公共许可证 (GPL) 第 3 版或更高版本发布。您可以自由地使用、修改和分发本项目的代码。

本项目适用于本科及以上计算机科学与技术相关专业的教学与实验；

你可以自由使用本项目的代码，但禁止将本项目的代码用于商业用途。
