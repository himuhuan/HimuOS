# KePool

固定大小内核对象池（slab-like bootstrap arena），以后备 `kernel heap foundation`
提供的 KVA-backed 页作为增长来源。当前实现不再直接向 `KePmmAllocPages()`
请求 backing page，而是统一通过 `KeHeapAllocPages()` 从 heap arena 获取页级空间。

`KePool` 拥有完整的 `Init → [Alloc / Free]* → Destroy` 生命周期契约，并通过
`KePoolQueryStats()` 提供持久的可观测统计面。该组件定位为 KVA heap foundation 之上
的固定大小对象池，不承担 slab / object-cache 后端的职责。

## 特性

1. **零对象开销**：通过时间复用内存槽位，空闲槽位存储 `Next` 指针，分配时整个槽位作为原始数据交给调用者
2. **最小槽位大小**：槽位大小严格保证至少为 `sizeof(void*)`，即使请求的对象大小更小
3. **O(1) 复杂度**：分配和释放均为常数时间操作，仅涉及空闲链表头部的指针交换
4. **按页增长**：当需要扩容时，每次通过 `KeHeapAllocPages(1)` 获取 1 个新的 KVA-backed 页，并将其切分为固定大小的 slot
5. **完整生命周期**：支持 `KePoolDestroy()` 显式回收所有 backing page；销毁后可通过 `KePoolInit()` 重新初始化
6. **Backing-page 所有权追踪**：每个 backing page 的起始位置保留一个 `KE_POOL_PAGE_NODE` 头，形成侵入式单链表，使 destroy 能枚举并释放所有页

## 初始化依赖关系

| 阶段 | 相关 API | 与 KePool 的关系 |
|------|-----------|------------------|
| KVA 初始化 | `KeKvaInit()` | 建立 heap arena 与页级分配基础；这是 `KePoolInit()`/`KePoolAlloc()` 发生页级增长的前提 |
| 池初始化 | `KePoolInit()` | 计算 `SlotSize`、`SlotsPerPage`，并按 `initialCapacity` 向上取整到页数后预热 backing page |
| 上层对象池初始化 | 例如 `KeKThreadPoolInit()` | 这些初始化最终会委托到 `KePoolInit()`，因此必须排在 `KeKvaInit()` 之后 |

从 `src/kernel/init/init.c` 可以直接看到这一顺序：`KeKvaInit()` 在前，`KeKThreadPoolInit()` 在后。

## 数据结构

### KE_POOL

```c
typedef struct KE_POOL
{
    uint32_t Magic;                 // KE_POOL_MAGIC_ALIVE 或 KE_POOL_MAGIC_DEAD
    KE_POOL_FREE_NODE *FreeList;    // 空闲链表头
    KE_POOL_PAGE_NODE *PageList;    // Backing page 侵入式链表
    size_t SlotSize;                // 槽位大小（字节）
    uint32_t SlotsPerPage;          // 每页可用槽位数（扣除页头后）
    uint32_t TotalSlots;            // 总槽位数
    uint32_t UsedSlots;             // 已使用槽位数
    uint32_t PeakUsedSlots;         // 历史峰值已使用槽位数
    uint32_t FailedGrows;           // 累计增长失败次数
    uint32_t PageCount;             // 当前持有的 backing page 数
    const char *Name;               // 调试名称
} KE_POOL;
```

### KE_POOL_PAGE_NODE

```c
typedef struct KE_POOL_PAGE_NODE
{
    struct KE_POOL_PAGE_NODE *Next;  // 下一个 backing page
} KE_POOL_PAGE_NODE;
```

### KE_POOL_STATS

```c
typedef struct KE_POOL_STATS
{
    uint32_t TotalSlots;      // 总槽位数
    uint32_t UsedSlots;       // 已使用槽位数
    uint32_t FreeSlots;       // 可用槽位数 (TotalSlots - UsedSlots)
    uint32_t PageCount;       // backing page 数
    uint32_t PeakUsedSlots;   // 峰值已使用槽位数
    uint32_t FailedGrowCount; // 累计增长失败次数
} KE_POOL_STATS;
```

### KE_POOL_FREE_NODE

```c
typedef struct KE_POOL_FREE_NODE
{
    struct KE_POOL_FREE_NODE *Next; // 下一个空闲节点
} KE_POOL_FREE_NODE;
```

## 函数

### KePoolInit

初始化对象池，并为初始容量预先建立页级 backing。如果初始化过程中增长失败，
已获取的页会被回滚释放，池不会处于半初始化状态。

> 前置条件：`KeKvaInit()` 必须已经完成，确保 `kernel heap foundation`
> 可以为对象池提供页级 backing。

```c
HO_STATUS HO_KERNEL_API KePoolInit(
    KE_POOL *pool,
    size_t objectSize,
    uint32_t initialCapacity,
    const char *name
);
```

| 参数 | 描述 |
|------|------|
| `pool` | 要初始化的池结构 |
| `objectSize` | 每个对象的大小（字节） |
| `initialCapacity` | 预分配的最小槽位数；实现会按页向上取整，因此实际 `TotalSlots` 可能大于该值 |
| `name` | 调试用的描述名称 |

**返回码：**

| 返回码 | 描述 |
|--------|------|
| `EC_SUCCESS` | 成功 |
| `EC_ILLEGAL_ARGUMENT` | 计算出的 `SlotSize` 大于单页容量（扣除页头后），无法切分出任何 slot |
| 其他错误码 | `kernel heap foundation` 尚未就绪，或页级增长失败 |

成功的 `KePoolInit()` 将 `Magic` 设置为 `KE_POOL_MAGIC_ALIVE`，并从新的统计基线开始。
对于已销毁的池，可以再次调用 `KePoolInit()` 开始新的生命周期。

### KePoolAlloc

从池中分配一个对象（零初始化）。当空闲链表为空时，实现会尝试再次通过
`KeHeapAllocPages(1)` 增长 1 个 backing page。

对于已销毁的池（`Magic == KE_POOL_MAGIC_DEAD`），`KePoolAlloc` 直接返回 `NULL`，
不会隐式复活该池。

```c
HO_KERNEL_API void *KePoolAlloc(KE_POOL *pool);
```

| 参数 | 描述 |
|------|------|
| `pool` | 要分配的池 |

**返回值：** 指向分配的对象的指针；如果空闲链表耗尽且无法再从 `kernel heap foundation` 增长 1 页，则返回 `NULL`

### KePoolFree

将对象归还到池中。

```c
HO_KERNEL_API void KePoolFree(KE_POOL *pool, void *object);
```

| 参数 | 描述 |
|------|------|
| `pool` | 对象所属的池 |
| `object` | 要归还的对象，`NULL` 为无操作 |

> 注意：`KePoolFree()` 只会把 slot 挂回空闲链表，不会把其所在 backing page 释放回
> `kernel heap foundation`。按页回收由 `KePoolDestroy()` 统一处理。

### KePoolDestroy

显式销毁对象池，释放所有 backing page 回 KVA heap foundation。

```c
HO_KERNEL_API HO_STATUS KePoolDestroy(KE_POOL *pool);
```

| 参数 | 描述 |
|------|------|
| `pool` | 要销毁的池；必须已成功初始化且 `UsedSlots == 0` |

**前置条件：**

| 条件 | 违反时行为 |
|------|-----------|
| `pool->Magic == KE_POOL_MAGIC_ALIVE` | 返回 `EC_INVALID_STATE` |
| `pool->UsedSlots == 0` | 返回 `EC_INVALID_STATE`，池保持完整 |

**返回码：**

| 返回码 | 描述 |
|--------|------|
| `EC_SUCCESS` | 所有 backing page 已归还，池已标记为 DEAD |
| `EC_INVALID_STATE` | 池未初始化、已销毁、或仍有未归还对象 |

成功销毁后，后续 `KePoolAlloc()` 返回 `NULL`。可再次调用 `KePoolInit()` 开启新的生命周期。

### KePoolQueryStats

获取池的统计快照。

```c
HO_KERNEL_API void KePoolQueryStats(const KE_POOL *pool, KE_POOL_STATS *stats);
```

| 参数 | 描述 |
|------|------|
| `pool` | 要查询的池 |
| `stats` | 输出的统计快照 |

快照在临界区内一次性读取，保证内部一致性。

## 使用示例

```c
// 完整生命周期示例
KE_POOL TestPool;

HO_STATUS status = KePoolInit(&TestPool, sizeof(MY_OBJ), 16, "TestPool");
if (status != EC_SUCCESS) { /* handle error */ }

MY_OBJ *obj = (MY_OBJ *)KePoolAlloc(&TestPool);
// ... use obj ...
KePoolFree(&TestPool, obj);

// 查询统计
KE_POOL_STATS stats;
KePoolQueryStats(&TestPool, &stats);
// stats.PeakUsedSlots, stats.FailedGrowCount, ...

// 销毁（所有对象已归还）
status = KePoolDestroy(&TestPool);
// 之后可以 KePoolInit() 重新使用
```

## 分配器层次定位

```
PMM  →  KVA (heap/stack/fixmap arena)  →  KeHeapAllocPages()  →  KePool  →  具体对象池
```

`KePool` 是 KVA heap foundation 之上的固定大小对象池，不会演化为 slab 分配器或
object-cache 后端。未来的 slab/cache 工作将作为独立的更高层分配器，可以包装或共存
于 `KePool` 之上，但不需要 `KePool` 本身吸收 cache 策略。

## Shrink（可选扩展）

当前设计不强制实现 shrink。backing-page 链的侵入式头部已经为未来的
per-page free-slot 追踪预留了自然扩展点：如果后续需要在 `UsedSlots > 0` 时
识别并回收完全空闲的页，可以在 `KE_POOL_PAGE_NODE` 中增加 `UsedCount` 字段，
无需改变现有的 freelist 结构。

## 安全警告

由于空闲链表指针位于对象自身内部，任何 Use-After-Free (UAF) 或缓冲区溢出都会直接破坏池的内部链接，导致系统范围的不稳定。
