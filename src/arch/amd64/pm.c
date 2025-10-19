#include "arch/amd64/pm.h"

void
LoadCR3(HO_PHYSICAL_ADDRESS pml4PhysAddr)
{
    uint64_t cr3 = pml4PhysAddr & PAGE_MASK;
    __asm__ __volatile__("cli");
    __asm__ __volatile__("mov %0, %%cr3" ::"r"(cr3) : "memory");
    __asm__ __volatile__("sti");
}

HO_KERNEL_API uint64_t
CalcPagesToStoreEntries(uint64_t entries, uint64_t entrySize, uint64_t pageSize)
{
    uint64_t entriesPerPage = pageSize / entrySize;
    return (entries + entriesPerPage - 1) / entriesPerPage;
}

GLOBAL_DESCRIPTOR_TABLE *
SetupGdt(void *base, uint64_t size)
{
    if (size < sizeof(GLOBAL_DESCRIPTOR_TABLE))
    {
        return NULL; // Not enough memory
    }

    GLOBAL_DESCRIPTOR_TABLE *gdt = (GLOBAL_DESCRIPTOR_TABLE *)base;

    // Null Descriptor
    gdt->GdtEntries[0] = (GDT_ENTRY){0, 0, 0, 0, 0, 0};

    // Kernel Code Segment Descriptor
    gdt->GdtEntries[1] = (GDT_ENTRY){
        .LimitLow = 0,       // Ignored in 64-bit mode
        .BaseLow = 0,        // Ignored in 64-bit mode
        .BaseMiddle = 0,     // Ignored in 64-bit mode
        .Access = 0x9A,      // P=1, Ring 0, Code Segment, Executable, Readable,
        .Granularity = 0x20, // L=1 (64-bit), D/B=0, G=0
        .BaseHigh = 0        // Ignored in 64-bit mode
    };

    // Kernel Data Segment Descriptor
    gdt->GdtEntries[2] = (GDT_ENTRY){
        .LimitLow = 0,       // Ignored in 64-bit mode
        .BaseLow = 0,        // Ignored in 64-bit mode
        .BaseMiddle = 0,     // Ignored in 64-bit mode
        .Access = 0x92,      // P=1, Ring 0, Data
        .Granularity = 0xC0, // L=0, D/B=1 (32-bit), G=1
        .BaseHigh = 0        // Ignored in 64-bit mode
    };

    // User Code Segment Descriptor
    gdt->GdtEntries[3] = (GDT_ENTRY){
        .LimitLow = 0,       // Ignored in 64-bit mode
        .BaseLow = 0,        // Ignored in 64-bit mode
        .BaseMiddle = 0,     // Ignored in 64-bit mode
        .Access = 0xFA,      // P=1, Ring 3, Code
        .Granularity = 0x20, // L=1 (64-bit), D/B=0, G=0
        .BaseHigh = 0        // Ignored in 64-bit mode
    };

    // User Data Segment Descriptor
    gdt->GdtEntries[4] = (GDT_ENTRY){
        .LimitLow = 0,       // Ignored in 64-bit mode
        .BaseLow = 0,        // Ignored in 64-bit mode
        .BaseMiddle = 0,     // Ignored in 64-bit mode
        .Access = 0xF2,      // P=1, Ring 3
        .Granularity = 0xC0, // L=0, D/B=1 (32-bit), G=0
        .BaseHigh = 0        // Ignored in 64-bit mode
    };

    gdt->GdtPtr.Limit = sizeof(gdt->GdtEntries) - 1;
    gdt->GdtPtr.Base = (uint64_t)gdt->GdtEntries;
    
    uint16_t kcode = GDT_KRNL_CODE_SEL;
    uint16_t kdata = GDT_KRNL_DATA_SEL;

    asm volatile("lgdt %0" ::"m"(gdt->GdtPtr));
    asm volatile("movw %0, %%ds\n\t"
                 "movw %0, %%es\n\t"
                 "movw %0, %%ss\n\t"
                 "movw %0, %%fs\n\t"
                 "movw %0, %%gs\n\t"
                 :
                 : "r"(kdata)
                 : "memory");
    asm volatile(
        "pushq %[cs]\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        :
        : [cs] "r"((uint64_t)kcode)
        : "rax", "memory");
        
    return gdt;
}