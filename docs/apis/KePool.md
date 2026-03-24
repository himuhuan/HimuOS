# KePool

固定大小内核对象池（slab-like bootstrap arena）。

## 特性

1. **零对象开销**：通过时间复用内存槽位，空闲槽位存储 `Next` 指针，分配时整个槽位作为原始数据交给调用者
2. **最小槽位大小**：槽位大小严格保证至少为 `sizeof(void*)`，即使请求的对象大小更小
3. **O(1) 复杂度**：分配和释放均为常数时间操作，仅涉及空闲链表头部的指针交换

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

初始化对象池。

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
| `initialCapacity` | 预分配的最小槽位数 |
| `name` | 调试用的描述名称 |

**返回码：**

| 返回码 | 描述 |
|--------|------|
| `EC_SUCCESS` | 成功 |
| `EC_NOT_ENOUGH_MEMORY` | PMM 分配失败 |

### KePoolAlloc

从池中分配一个对象（零初始化）。

```c
HO_KERNEL_API void *KePoolAlloc(KE_POOL *pool);
```

| 参数 | 描述 |
|------|------|
| `pool` | 要分配的池 |

**返回值：** 指向分配的对象的指针，如果池和 PMM 耗尽则返回 `NULL`

### KePoolFree

将对象归还到池中。

```c
HO_KERNEL_API void KePoolFree(KE_POOL *pool, void *object);
```

| 参数 | 描述 |
|------|------|
| `pool` | 对象所属的池 |
| `object` | 要归还的对象，`NULL` 为无操作 |

## 使用示例

```c
// 定义线程池
KE_POOL ThreadPool;

// 初始化池，对象大小为 THREAD 结构体，初始容量 64
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
