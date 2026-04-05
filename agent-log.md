# 实施报告：priority-ready-queues

**日期**：2026-04-05  
**变更 ID**：priority-ready-queues  
**状态**：✅ 已完成，当前实现已提交到本地 `main`

---

## 1. 变更目标

为 HimuOS 内核调度器引入固定 3 级优先级 ready queue（LOW / NORMAL / HIGH），替换原有的单条 ready 链表。具体目标：

- `KTHREAD_PRIORITY_LOW = 0`、`KTHREAD_PRIORITY_NORMAL = 1`、`KTHREAD_PRIORITY_HIGH = 2`，新线程默认 NORMAL；
- dispatch 时始终选最高非空优先级队列的队头，调度选择点上 HIGH 优先于 LOW/NORMAL；
- 同一优先级队列内保持原有 round-robin + quantum 机制不变；
- wait/sync 唤醒路径按线程自身优先级回队，不再统一入同一链表；
- `KE_SYSINFO_SCHEDULER_DATA` 增加 `ReadyQueueDepthByPriority[KTHREAD_PRIORITY_COUNT]`，同时保留 `ReadyQueueDepth` 总量；
- 在已有 `schedule` demo profile 中增加 priority smoke 测试，不新建独立 profile。

---

## 2. Phase 执行摘要

| Phase | Commit | 主要内容 |
|---|---|---|
| P1：基础结构 | `6839a81` | 引入 `gReadyQueues[KTHREAD_PRIORITY_COUNT]` 数组，`KTHREAD.Priority` 字段，`scheduler_internal.h` 中的全套内联辅助函数（`KiGetReadyQueueForPriority`、`KiGetHighestPriorityReadyQueue`、`KiCountAllReadyThreads` 等），将旧单队列改写为按优先级初始化的多队列 |
| P2：dispatch 改写 | `68bfe41` | `KiSchedule` 从 `KiGetHighestPriorityReadyQueue()` 选队列；timer ISR preemption 路径将当前线程按其优先级送回对应队列 |
| P3：唤醒对齐与可观测性 | `54e3241` | `wait.c` 中的 `KiCompleteWait` 改为按线程优先级回队，`sync.c` 的 idle wake 判定切到全队列检查；`KE_SYSINFO_SCHEDULER_DATA` 增加 `ReadyQueueDepthByPriority` 数组；`init.c` 自测补充 per-priority depth 断言；`diag.c` 诊断输出对应更新 |
| P4：smoke 测试 | `c7c6241` | `src/kernel/demo/thread.c` 新增 `priority_order_test`（HIGH 先于 LOW 执行验证）和 `priority_rr_test`（同 HIGH 优先级 ABABAB round-robin 验证）；`demo.c` / `demo_internal.h` 接入 schedule profile |

总计改动文件 13 个，新增 362 行、删除 18 行。

---

## 3. 实际提交

```
6839a81  Add scheduler priority queue foundation
68bfe41  Add priority-aware scheduler dispatch
54e3241  Align scheduler wakeups and observability
c7c6241  Add scheduler priority smoke coverage
```

---

## 4. 关键实现点

**多队列数据结构**  
`scheduler_internal.h` 将原来单个 `gReadyQueue` 换为 `gReadyQueues[KTHREAD_PRIORITY_COUNT]`（外部声明，在 `scheduler.c` 中定义）。所有入队/出队操作通过 `KiGetReadyQueueForPriority` / `KiGetReadyQueueForThread` 路由，消除了散落各处的直接链表操作。

**最高优先级选择**  
`KiGetHighestPriorityReadyQueue()` 从 `KTHREAD_PRIORITY_HIGH` 向 `KTHREAD_PRIORITY_LOW` 遍历，返回第一个非空队列的指针。`KiSchedule` 直接从该队列取队头，保证严格优先级抢占，时间复杂度 O(KTHREAD_PRIORITY_COUNT)，即 O(3)。

**preemption 回队路径**  
timer ISR 触发 preemption 时，原来总是入同一个链表；修改后调用 `KiGetReadyQueueForThread(current)` 送回当前线程对应的优先级队列，RR 语义不变。

**唤醒路径对齐**  
`wait.c` 中的 `KiCompleteWait` 改用 `KiGetReadyQueueForThread`，确保从 sleep/wait 恢复的线程回到与其优先级匹配的 ready queue；`sync.c` 中 idle 场景下的 need-schedule 判定也切到 `KiHasAnyReadyThread()`，不再只盯默认队列。

**sysinfo 可观测性**  
`KE_SYSINFO_SCHEDULER_DATA.ReadyQueueDepthByPriority[KTHREAD_PRIORITY_COUNT]` 由 `KeQuerySchedulerInfo` 填充；`init.c` 的 scheduler observability 自测新增了“初始化后各优先级 ready depth 全为 0”的断言。

---

## 5. 最终验证结果

验证日志：`/tmp/himuos-schedule-final.log`（共 680 行）

### Priority smoke（第 86–135 行）

**优先级顺序子测试**

| 行号 | 锚点 |
|---|---|
| 86 | `[PRIO] smoke start` |
| 95 | `[PRIO] order slot=0 token=H step=0 thread=3 priority=2` |
| 101 | `[PRIO] order slot=1 token=L step=0 thread=2 priority=0` |
| 107 | `[PRIO] order observed=HL expected=HL` |
| 108 | `[PRIO] order passed` |

HIGH（priority=2）线程在 LOW（priority=0）线程之前完成，调度顺序符合预期。

**同优先级 Round-Robin 子测试**

| 行号 | 锚点 |
|---|---|
| 117 | `rr slot=0 token=A priority=2` |
| 119 | `rr slot=1 token=B priority=2` |
| 121 | `rr slot=2 token=A priority=2` |
| 123 | `rr slot=3 token=B priority=2` |
| 125 | `rr slot=4 token=A priority=2` |
| 129 | `rr slot=5 token=B priority=2` |
| 133 | `[PRIO] rr observed=ABABAB expected=ABABAB` |
| 134 | `[PRIO] rr passed` |
| 135 | `[PRIO] smoke passed` |

两个同为 HIGH 优先级的线程严格交替调度，RR 语义在多队列结构下保持正确。

### Thread termination collaboration（第 675 行）

```
[TEST] thread termination collaboration demo passed
```

原有 join/detach/timeout 全套测试在多队列调度器下通过，无回归。

---

## 6. 本次未纳入的事项

- 优先级动态调整接口（如 `KeSetThreadPriority` 之类的公开 API）未实现，本次只实现了调度器内部消费 `KTHREAD.Priority`；
- 优先级继承（用于 mutex 防止 priority inversion）未实现；
- 用户态线程优先级透传接口未实现；
- `KTHREAD_PRIORITY_COUNT` 固定为 3，扩展优先级级数需改动 enum 及相关数组，未做抽象。
