#include "arch/amd64/pm.h"

static void SetTssEntry(void *base, uint64_t tss);

void
LoadCR3(HO_PHYSICAL_ADDRESS pml4PhysAddr)
{
    uint64_t cr3 = pml4PhysAddr & PAGE_MASK;
    __asm__ __volatile__("mov %0, %%cr3" ::"r"(cr3) : "memory");
}

HO_KERNEL_API uint64_t
CalcPagesToStoreEntries(uint64_t entries, uint64_t entrySize, uint64_t pageSize)
{
    uint64_t entriesPerPage = pageSize / entrySize;
    return (entries + entriesPerPage - 1) / entriesPerPage;
}

HO_KERNEL_API HO_NODISCARD CPU_CORE_LOCAL_DATA *
InitCpuCoreLocalData(void *base, uint64_t size)
{
    if (size < sizeof(CPU_CORE_LOCAL_DATA))
    {
        return NULL; // Not enough memory
    }

    CPU_CORE_LOCAL_DATA *gdt = (CPU_CORE_LOCAL_DATA *)base;
    memset(gdt, 0, sizeof(CPU_CORE_LOCAL_DATA));

    // Null Descriptor
    gdt->GdtEntries[0] = (GDT_ENTRY){0, 0, 0, 0, 0, 0};

    // Kernel Code Segment Descriptor
    gdt->GdtEntries[GDT_KRNL_CODE_INDEX] = (GDT_ENTRY){
        .LimitLow = 0,       // Ignored in 64-bit mode
        .BaseLow = 0,        // Ignored in 64-bit mode
        .BaseMiddle = 0,     // Ignored in 64-bit mode
        .Access = 0x9A,      // P=1, Ring 0, Code Segment, Executable, Readable,
        .Granularity = 0x20, // L=1 (64-bit), D/B=0, G=0
        .BaseHigh = 0        // Ignored in 64-bit mode
    };

    // Kernel Data Segment Descriptor
    gdt->GdtEntries[GDT_KRNL_DATA_INDEX] = (GDT_ENTRY){
        .LimitLow = 0,       // Ignored in 64-bit mode
        .BaseLow = 0,        // Ignored in 64-bit mode
        .BaseMiddle = 0,     // Ignored in 64-bit mode
        .Access = 0x92,      // P=1, Ring 0, Data
        .Granularity = 0xC0, // L=0, D/B=1 (32-bit), G=1
        .BaseHigh = 0        // Ignored in 64-bit mode
    };

    // User Code Segment Descriptor
    gdt->GdtEntries[GDT_USER_CODE_INDEX] = (GDT_ENTRY){
        .LimitLow = 0,       // Ignored in 64-bit mode
        .BaseLow = 0,        // Ignored in 64-bit mode
        .BaseMiddle = 0,     // Ignored in 64-bit mode
        .Access = 0xFA,      // P=1, Ring 3, Code
        .Granularity = 0x20, // L=1 (64-bit), D/B=0, G=0
        .BaseHigh = 0        // Ignored in 64-bit mode
    };

    // User Data Segment Descriptor
    gdt->GdtEntries[GDT_USER_DATA_INDEX] = (GDT_ENTRY){
        .LimitLow = 0,       // Ignored in 64-bit mode
        .BaseLow = 0,        // Ignored in 64-bit mode
        .BaseMiddle = 0,     // Ignored in 64-bit mode
        .Access = 0xF2,      // P=1, Ring 3
        .Granularity = 0xC0, // L=0, D/B=1 (32-bit), G=0
        .BaseHigh = 0        // Ignored in 64-bit mode
    };

    // TSS Descriptor
    SetTssEntry(&gdt->GdtEntries[GDT_TSS_INDEX], (uint64_t)&gdt->Tss);

    gdt->GdtPtr.Limit = sizeof(gdt->GdtEntries) - 1;
    gdt->GdtPtr.Base = (uint64_t)gdt->GdtEntries;

    return gdt;
}

HO_KERNEL_API void LoadGdtAndTss(CPU_CORE_LOCAL_DATA *data)
{
    uint16_t kcode = GDT_KRNL_CODE_SEL;
    uint16_t kdata = GDT_KRNL_DATA_SEL;
    uint16_t ktss = GDT_TSS_SEL;

    asm volatile("lgdt %0" ::"m"(data->GdtPtr));
    asm volatile("movw %0, %%ds\n\t"
                 "movw %0, %%es\n\t"
                 "movw %0, %%ss\n\t"
                 "movw %0, %%fs\n\t"
                 "movw %0, %%gs\n\t"
                 :
                 : "r"(kdata)
                 : "memory");
    asm volatile("ltr %0" ::"r"(ktss) : "memory");
    asm volatile("pushq %[cs]\n\t"
                 "leaq 1f(%%rip), %%rax\n\t"
                 "pushq %%rax\n\t"
                 "lretq\n\t"
                 "1:\n\t"
                 :
                 : [cs] "r"((uint64_t)kcode)
                 : "rax", "memory");
}

static void SetTssEntry(void *base, uint64_t tss)
{
    TSS_DESCRIPTOR *desc = base;
    memset(desc, 0, sizeof(TSS_DESCRIPTOR));

    const uint16_t kLimit = sizeof(TSS64) - 1;
    desc->LimitLow = kLimit & 0xFFFF;
    desc->BaseLow = tss & 0xFFFF;
    desc->BaseMiddle = (uint8_t) ((tss >> 16) & 0xFF);
    desc->Attributes = 0x89; // P=1, DPL=0, Type=9 (Available 64-bit TSS)
    // G = 0, D/B = 0, L = 0, AVL = 0, LimitHigh = (kLimit >> 16) & 0x0F
    desc->LimitHighAndFlags = (kLimit >> 16) & 0x0F;
    desc->BaseHigh = (uint8_t) ((tss >> 24) & 0xFF);
    desc->BaseUpper = (uint32_t) (tss >> 32);
    desc->Reserved = 0;
}
