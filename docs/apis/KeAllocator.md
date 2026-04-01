# KeAllocator

`KeAllocator` 是位于 `KeHeapAllocPages()` 之上的通用分配层。
当前仓库中的内存分层已经明确为：

`PMM -> KVA(heap arena) -> KeHeapAllocPages() -> KeAllocator / KePool`

`KeAllocator` 与 `KePool` 并列共存，不共享生命周期与统计口径。

## 分层关系

- `KeAllocator`：面向可变大小对象/缓冲区的通用入口。
- `KePool`：固定大小对象池，保留独立 `Init/Alloc/Free/Destroy/QueryStats` 合同。

## 初始化顺序

`KeAllocatorInit()` 依赖 KVA heap foundation，且在 pool-backed 子系统之前完成接线。

当前启动关键顺序：

1. `KeKvaInit()`
2. `KeKvaSelfTest()`
3. `RunMemoryObservabilitySelfTest()`
4. `KeAllocatorInit()`
5. `RunAllocatorObservabilitySelfTest()`
6. `ConsolePromoteAllocatorStorage()`
7. `KeKThreadPoolInit()` / `KePoolInit()` 路径

## API

```c
typedef struct KE_ALLOCATOR_STATS
{
    uint64_t LiveAllocationCount;
    uint64_t LiveSmallAllocationCount;
    uint64_t LiveLargeAllocationCount;
    uint64_t BackingBytes;
    uint64_t FailedAllocationCount;
} KE_ALLOCATOR_STATS;

HO_STATUS KeAllocatorInit(void);
HO_STATUS KeAllocatorQueryStats(KE_ALLOCATOR_STATS *outStats);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);
```

## 当前实现状态

- `KeAllocatorQueryStats()`：在 `KeAllocatorInit()` 前返回 `EC_INVALID_STATE`。
- `KeAllocatorInit()` 之后，`KeAllocatorQueryStats()` 返回实时快照（live allocation/small/large/backing/failure）。
- `kmalloc()` / `kzalloc()`：
  - 小对象走 size class（`16/32/64/128/256/512/1024`）。
  - 超过 small range 的请求走 dedicated heap-backed range（`KeHeapAllocPages()` fallback）。
- `kfree(NULL)`：永远 no-op。
- allocator 诊断由 `KeAllocatorDiagnoseAddress()` 提供；`KeDiagnoseVirtualAddress()` 仅在地址已被 KVA 判定为 `active-heap` 后追加 allocator-owned meaning。

## 阶段四兼容与试点迁移

- `KePool` 与 `KeAllocator` 在本变更中保持并存。
- 低风险 pilot 迁移为 `MUX_CONSOLE_SINK`：
  - 早期 `ConsoleInit()` 继续使用 bootstrap 存储，不依赖 allocator 就绪。
  - allocator 自检通过后，再执行 `ConsolePromoteAllocatorStorage()` 迁移到 allocator-owned storage。
- 高价值但生命周期更重的 consumer（例如 `KTHREAD` / `KePool` 路径）本次明确不迁移，这是显式 rollout 决策，不是遗漏。
