#include "init.h"
#include "interrupt.h"
#include "lib/device/clock_irq.h"
#include "lib/kernel/krnlio.h"
#include "task/sched.h"
#include "memory.h"

void InitKernel(void) {
    ClearScreen();
    PrintStr("HimuOS Version DEV 0.0.1\n");
    PrintStr("CopyRight (C) 2024 HimuOS Project, all rights reserved\n");
    PrintStr("==============================================================================\n\n\n");

    PrintStr("Initilizing HimuOS Kernel...\n");
    InitIdt();
    InitClockIrq();
    InitThread();
    KrnlMemInit();
}
