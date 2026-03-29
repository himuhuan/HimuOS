# KePool

固定大小内核对象池（slab-like bootstrap arena），以后备 `kernel heap foundation`
提供的 KVA-backed 页作为增长来源。当前实现不再直接向 `KePmmAllocPages()`
请求 backing page，而是统一通过 `KeHeapAllocPages()` 从 heap arena 获取页级空间。

## 特性

1. **零对象开销**：通过时间复用内存槽位，空闲槽位存储 `Next` 指针，分配时整个槽位作为原始数据交给调用者
2. **最小槽位大小**：槽位大小严格保证至少为 `sizeof(void*)`，即使请求的对象大小更小
3. **O(1) 复杂度**：分配和释放均为常数时间操作，仅涉及空闲链表头部的指针交换
4. **按页增长**：当需要扩容时，每次通过 `KeHeapAllocPages(1)` 获取 1 个新的 KVA-backed 页，并将其切分为固定大小的 slot

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
    KE_POOL_FREE_NODE *FreeList;    // 空闲链表头
    size_t SlotSize;                // 槽位大小（字节）
    uint32_t SlotsPerPage;          // 每页槽位数
    uint32_t TotalSlots;            // 总槽位数
    uint32_t UsedSlots;             // 已使用槽位数
    const char *Name;               // 调试名称
} KE_POOL;
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

初始化对象池，并为初始容量预先建立页级 backing。

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
| `EC_ILLEGAL_ARGUMENT` | 计算出的 `SlotSize` 大于单页容量，无法切分出任何 slot |
| 其他错误码 | `kernel heap foundation` 尚未就绪，或页级增长失败 |

### KePoolAlloc

从池中分配一个对象（零初始化）。当空闲链表为空时，实现会尝试再次通过
`KeHeapAllocPages(1)` 增长 1 个 backing page。

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
> `kernel heap foundation`。这一点也与当前实现中尚无 shrink/destroy API 相一致。

## 使用示例

```c
// 定义线程池
KE_POOL ThreadPool;

// 在 KeKvaInit() 完成后初始化池；KePool 会通过 KeHeapAllocPages() 建立 backing page
HO_STATUS status = KePoolInit(&ThreadPool, sizeof(THREAD), 64, "ThreadPool");
if (status != EC_SUCCESS) {
    // 处理初始化失败
}

// 分配一个线程对象
THREAD *thread = (THREAD *)KePoolAlloc(&ThreadPool);
if (thread != NULL) {
    // 初始化线程
    thread->Id = nextThreadId++;
    // ... 其他初始化
}

// 使用完成后归还
KePoolFree(&ThreadPool, thread);
```

## 安全警告

由于空闲链表指针位于对象自身内部，任何 Use-After-Free (UAF) 或缓冲区溢出都会直接破坏池的内部链接，导致系统范围的不稳定。
