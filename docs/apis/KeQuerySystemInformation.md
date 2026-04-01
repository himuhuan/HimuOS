# KeQuerySystemInformation

统一的系统信息查询 API。

## 函数签名

```c
HO_STATUS HO_KERNEL_API KeQuerySystemInformation(
    KE_SYSINFO_CLASS Class,
    void            *Buffer,
    size_t           BufferSize,
    size_t          *RequiredSize
);
```

## 参数

| 参数 | 描述 |
|------|------|
| `Class` | 要查询的信息类别 |
| `Buffer` | 输出缓冲区，可为 NULL 以仅查询所需大小 |
| `BufferSize` | 缓冲区大小（字节） |
| `RequiredSize` | 可选，返回所需的缓冲区大小 |

## 信息类别

| Class | 返回结构 | 描述 |
|-------|---------|------|
| `KE_SYSINFO_BOOT_MEMORY_MAP` | EFI_MEMORY_MAP | 启动时内存映射（仅 Debug 构建） |
| `KE_SYSINFO_CPU_BASIC` | ARCH_BASIC_CPU_INFO | CPU 基本信息 |
| `KE_SYSINFO_CPU_FEATURES` | SYSINFO_CPU_FEATURES | CPU 特性标志（CPUID） |
| `KE_SYSINFO_PAGE_TABLE` | SYSINFO_PAGE_TABLE | 当前页表 CR3 |
| `KE_SYSINFO_PHYSICAL_MEM_STATS` | SYSINFO_PHYSICAL_MEM_STATS | PMM 物理内存统计（总量/空闲/已分配/保留） |
| `KE_SYSINFO_VIRTUAL_LAYOUT` | SYSINFO_VIRTUAL_LAYOUT | 虚拟地址空间布局 |
| `KE_SYSINFO_GDT` | SYSINFO_GDT | GDT 内容 |
| `KE_SYSINFO_TSS` | TSS64 | TSS 内容 |
| `KE_SYSINFO_IDT` | SYSINFO_IDT | IDT 基址和限长 |
| `KE_SYSINFO_TIME_SOURCE` | SYSINFO_TIME_SOURCE | 当前时间源信息 |
| `KE_SYSINFO_UPTIME` | SYSINFO_UPTIME | 系统运行时间（纳秒） |
| `KE_SYSINFO_SYSTEM_VERSION` | SYSINFO_SYSTEM_VERSION | 系统版本信息 |
| `KE_SYSINFO_CLOCK_EVENT` | SYSINFO_CLOCK_EVENT | 当前时钟事件设备状态 |
| `KE_SYSINFO_SCHEDULER` | KE_SYSINFO_SCHEDULER_DATA | 调度器状态快照 |
| `KE_SYSINFO_VMM_OVERVIEW` | SYSINFO_VMM_OVERVIEW | VMM 总览（imported/KVA/fixmap/heap） |
| `KE_SYSINFO_ACTIVE_KVA_RANGES` | SYSINFO_ACTIVE_KVA_RANGES | 活跃 KVA range 的有界快照 |

## 返回结构体

### SYSINFO_CPU_FEATURES

```c
typedef struct SYSINFO_CPU_FEATURES {
    uint32_t Leaf1_ECX;       // CPUID.01H:ECX
    uint32_t Leaf1_EDX;       // CPUID.01H:EDX
    uint32_t Leaf7_EBX;       // CPUID.07H:EBX
    uint32_t Leaf7_ECX;       // CPUID.07H:ECX
    uint32_t ExtLeaf1_ECX;    // CPUID.80000001H:ECX
    uint32_t ExtLeaf1_EDX;    // CPUID.80000001H:EDX
} SYSINFO_CPU_FEATURES;
```

### SYSINFO_VIRTUAL_LAYOUT

```c
typedef struct SYSINFO_VIRTUAL_LAYOUT {
    HO_VIRTUAL_ADDRESS KernelBase;   // 内核基址
    HO_VIRTUAL_ADDRESS KernelStack;  // 内核栈基址
    HO_VIRTUAL_ADDRESS HhdmBase;     // HHDM 基址
    HO_VIRTUAL_ADDRESS MmioBase;     // MMIO 基址
} SYSINFO_VIRTUAL_LAYOUT;
```

### SYSINFO_PHYSICAL_MEM_STATS

```c
typedef struct SYSINFO_PHYSICAL_MEM_STATS {
    uint64_t TotalBytes;
    uint64_t FreeBytes;
    uint64_t AllocatedBytes;
    uint64_t ReservedBytes;
} SYSINFO_PHYSICAL_MEM_STATS;
```

说明：该查询由 `KePmmQueryStats()` 提供数据；在 PMM 尚未初始化时返回 `EC_INVALID_STATE`。

### SYSINFO_VMM_OVERVIEW

```c
typedef struct SYSINFO_VMM_ARENA_OVERVIEW {
    uint64_t TotalPages;
    uint64_t FreePages;
    uint64_t ActiveAllocations;
} SYSINFO_VMM_ARENA_OVERVIEW;

typedef struct SYSINFO_VMM_OVERVIEW {
    uint32_t ImportedRegionCount;
    uint32_t Reserved0;
    SYSINFO_VMM_ARENA_OVERVIEW StackArena;
    SYSINFO_VMM_ARENA_OVERVIEW FixmapArena;
    SYSINFO_VMM_ARENA_OVERVIEW HeapArena;
    uint64_t ActiveKvaRangeCount;
    uint64_t FixmapTotalSlots;
    uint64_t FixmapActiveSlots;
} SYSINFO_VMM_OVERVIEW;
```

说明：
- `ImportedRegionCount` 来自 imported kernel address space 的 region 目录。
- 三个 arena 统计来自 `KeKvaQueryArenaInfo()`。
- `ActiveKvaRangeCount/Fixmap*` 来自 `KeKvaQueryUsageInfo()`。
- 若地址空间或 KVA 尚未初始化，查询返回 `EC_INVALID_STATE`。

### SYSINFO_ACTIVE_KVA_RANGES

```c
#define SYSINFO_ACTIVE_KVA_RANGE_MAX 16U

typedef struct SYSINFO_ACTIVE_KVA_RANGE {
    KE_KVA_ARENA_TYPE Arena;
    uint32_t RecordId;
    uint64_t Generation;
    HO_VIRTUAL_ADDRESS BaseAddress;
    HO_VIRTUAL_ADDRESS EndAddressExclusive;
    HO_VIRTUAL_ADDRESS UsableBase;
    HO_VIRTUAL_ADDRESS UsableEndExclusive;
    uint64_t TotalPages;
    uint64_t UsablePages;
    uint64_t GuardLowerPages;
    uint64_t GuardUpperPages;
} SYSINFO_ACTIVE_KVA_RANGE;

typedef struct SYSINFO_ACTIVE_KVA_RANGES {
    uint64_t TotalActiveRangeCount;
    uint32_t ReturnedRangeCount;
    BOOL Truncated;
    SYSINFO_ACTIVE_KVA_RANGE Ranges[SYSINFO_ACTIVE_KVA_RANGE_MAX];
} SYSINFO_ACTIVE_KVA_RANGES;
```

说明：
- 该查询返回一个固定上限为 `16` 条记录的稳定快照，用 arena 与虚拟地址范围语义描述当前活跃 KVA range。
- `RecordId + 64-bit Generation` 共同标识当前这一次 live range 实例；仅凭 `RecordId` 不能跨释放/复用周期充当稳定所有权句柄。
- `TotalActiveRangeCount` 表示系统中当前活跃 range 总数，`ReturnedRangeCount` 表示本次快照实际返回条数。
- 当活跃 range 多于快照容量时，`Truncated == TRUE`，调用者必须把它解释为“有更多活跃 range 未被当前快照承载”，而不是“不存在更多 range”。
- KVA 未初始化时该查询返回 `EC_INVALID_STATE`；它不会输出部分伪造记录。

### SYSINFO_CLOCK_EVENT

```c
typedef struct SYSINFO_CLOCK_EVENT {
    BOOL Ready;
    uint64_t FreqHz;
    uint64_t InterruptCount;
    uint64_t MinDeltaNs;
    uint64_t MaxDeltaNs;
    uint8_t VectorNumber;
    char SourceName[SYSINFO_TIME_SOURCE_NAME_LEN];
} SYSINFO_CLOCK_EVENT;
```

说明：
- `Ready == FALSE` 时，查询仍返回 `EC_SUCCESS`，但不会伪造 source/vector/frequency 或 one-shot envelope。
- `MinDeltaNs/MaxDeltaNs` 描述活动 clock-event 设备当前支持的 one-shot 编程边界。
- scheduler 自己打算驱动的下一次 deadline 仍通过 `KE_SYSINFO_SCHEDULER.NextProgrammedDeadline` 查询，而不是塞回 clock-event 设备快照。

### KE_SYSINFO_SCHEDULER_DATA

```c
typedef struct KE_SYSINFO_SCHEDULER_DATA {
    BOOL SchedulerEnabled;
    uint32_t CurrentThreadId;
    uint32_t IdleThreadId;
    uint32_t ReadyQueueDepth;
    uint32_t SleepQueueDepth;
    uint32_t ActiveThreadCount;
    uint64_t EarliestWakeDeadline;
    uint64_t NextProgrammedDeadline;
    uint64_t ContextSwitchCount;
    uint64_t PreemptionCount;
    uint64_t YieldCount;
    uint64_t SleepWakeCount;
    uint64_t TotalThreadsCreated;
} KE_SYSINFO_SCHEDULER_DATA;
```

说明：
- `SleepQueueDepth` 表示全局 timeout-backed queue 的深度，覆盖 `KeSleep()` 和带有限 deadline 的 dispatcher wait。
- `EarliestWakeDeadline` 是 timeout queue 队首最早绝对 deadline；无等待项时为 `0`。
- `NextProgrammedDeadline` 反映 scheduler 当前打算驱动的下一次绝对 deadline；系统真正 idle 且无 timeout-backed wait 时为 `0`。
- `SleepWakeCount` 统计 timeout 路径唤醒次数，不把对象 signal 立即满足计入 timeout 唤醒。

### SYSINFO_UPTIME

```c
typedef struct SYSINFO_UPTIME {
    uint64_t Nanoseconds;  // 系统运行时间（纳秒）
} SYSINFO_UPTIME;
```

## 返回码

| 返回码 | 描述 |
|--------|------|
| `EC_SUCCESS` | 成功 |
| `EC_ILLEGAL_ARGUMENT` | 无效的 Class |
| `EC_NOT_ENOUGH_MEMORY` | 缓冲区不足 |
| `EC_NOT_SUPPORTED` | 该类别在当前构建不可用 |
| `EC_INVALID_STATE` | 所需子系统未初始化 |

## 相关诊断接口（非本 API）

以下接口不属于 `KeQuerySystemInformation`，但与本次内存可观测性变更配套：

- `KeDiagnoseVirtualAddress()`：组合 imported-region、PT query、KVA 分类及各层状态，返回统一的 `KE_VA_DIAGNOSIS`。
- 页故障蓝屏输出：在 vector-14 下始终先输出寄存器转储、`CR2` 与 `PFERR` 位域；只有进入 dedicated `IST2` 安全诊断上下文后，才追加 `VMM imported / VMM pt / VMM kva`。若地址已被 KVA 判定为 `active-heap`，再追加 `VMM allocator` 解释层。若某层不可用，输出会显式标示 `unavailable`，而不是猜测成功分类。

## 启动期联动验证

`InitKernel` 中的 observability self-test 会在启动时验证：

1. `KE_SYSINFO_PHYSICAL_MEM_STATS` 与 `KE_SYSINFO_VMM_OVERVIEW` 可成功查询并建立基线。
2. `KE_SYSINFO_ACTIVE_KVA_RANGES` 能以有界快照方式反映 fixmap 与 heap 的真实活跃 range，并在需要时通过 `Truncated` 表达省略。
3. 临时 fixmap 映射与 heap 分配会推动 PMM/VMM 计数按预期变化。
4. allocator 自检会覆盖 small/large 分配、`kzalloc` 零初始化、统计变化、allocator-aware diagnosis 与释放后回基线。
5. allocator 自检通过后，`ConsolePromoteAllocatorStorage()` 才会把 mux sink 列表提升到 allocator-owned storage。
6. clock-event 就绪后，`KE_SYSINFO_CLOCK_EVENT` 会暴露 source/vector/frequency 与有效的 `MinDeltaNs/MaxDeltaNs` 编程边界。
7. scheduler 初始化后，`KE_SYSINFO_SCHEDULER` 会反映 idle 基线：`CurrentThreadId == IdleThreadId == 0`、无 ready/sleep backlog、无 pending deadline。
8. 所有联动验证完成后，关键计数回归基线，确保无泄漏和账本漂移。

## 使用示例

```c
// 查询所需大小
size_t required;
KeQuerySystemInformation(KE_SYSINFO_CPU_FEATURES, NULL, 0, &required);

// 获取 CPU 特性
SYSINFO_CPU_FEATURES features;
HO_STATUS status = KeQuerySystemInformation(
    KE_SYSINFO_CPU_FEATURES,
    &features,
    sizeof(features),
    NULL
);

if (status == EC_SUCCESS) {
    // 检查 SSE4.2 支持 (Leaf1_ECX bit 20)
    if (features.Leaf1_ECX & (1 << 20)) {
        // SSE4.2 available
    }
}

// 获取系统运行时间
SYSINFO_UPTIME uptime;
KeQuerySystemInformation(KE_SYSINFO_UPTIME, &uptime, sizeof(uptime), NULL);
uint64_t seconds = uptime.Nanoseconds / 1000000000ULL;
```
