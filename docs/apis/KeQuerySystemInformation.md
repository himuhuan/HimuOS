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
| `KE_SYSINFO_PHYSICAL_MEM_STATS` | SYSINFO_PHYSICAL_MEM_STATS | 物理内存统计（预留） |
| `KE_SYSINFO_VIRTUAL_LAYOUT` | SYSINFO_VIRTUAL_LAYOUT | 虚拟地址空间布局 |
| `KE_SYSINFO_GDT` | SYSINFO_GDT | GDT 内容 |
| `KE_SYSINFO_TSS` | TSS64 | TSS 内容 |
| `KE_SYSINFO_IDT` | SYSINFO_IDT | IDT 基址和限长 |
| `KE_SYSINFO_TIME_SOURCE` | SYSINFO_TIME_SOURCE | 当前时间源信息 |
| `KE_SYSINFO_UPTIME` | SYSINFO_UPTIME | 系统运行时间（纳秒） |
| `KE_SYSINFO_SYSTEM_VERSION` | SYSINFO_SYSTEM_VERSION | 系统版本信息 |

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
