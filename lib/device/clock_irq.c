#include "clock_irq.h"
#include "krnlio.h"
#include "interrupt.h"
#include "task/sched.h"
#include "krnldbg.h"
#include "lib/asm/i386asm.h"
#include "lib/device/clock_irq.h"
#include "lib/kernel/krnlio.h"

#define INPUT_FREQUENCY  1193180
#define CONTRER0_PORT    0x40
#define COUNTER0_NO      0
#define IRQ0_HZ          100
#define IRQ0_FREQUENCY   (INPUT_FREQUENCY / IRQ0_HZ)
#define COUNTER_MODE     2
#define READ_WRITE_LATCH 3
#define PIT_CONTROL_PORT 0x43

uint32_t gTotalTicks;

static void ClockIrqHandler(void) {
    struct KR_TASK_STRUCT *currThread = KrGetRunningThreadPcb();
    KASSERT(currThread->StackCanary == PCB_CANARY_MAGIC);

    currThread->ElapsedTicks++;
    gTotalTicks++;

    if (currThread->RemainingTicks == 0) {
        KrDefaultSchedule();
    } else {
        currThread->RemainingTicks--;
    }
}

void InitClockIrq(void) {
    PrintStr("InitClockIrq START\n");
    SetClockIrq();
    KrRegisterIntrHandler(0x20, ClockIrqHandler);
    PrintStr("InitClockIrq DONE!\n");
}

void SetClockIrq(void) {
    PrintStr("SetClockIrq...");
    outb(PIT_CONTROL_PORT, (uint8_t)(COUNTER0_NO << 6 | READ_WRITE_LATCH << 4 | COUNTER_MODE << 1));
    outb(CONTRER0_PORT, (uint8_t)IRQ0_FREQUENCY);
    outb(CONTRER0_PORT, (uint8_t)(IRQ0_FREQUENCY >> 8));
    PrintStr("DONE\n");
}
