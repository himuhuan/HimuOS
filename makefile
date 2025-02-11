# HIMU OPERATING SYSTEM
# Copyright (C) 2024 HimuOS Project, all rights reserved.

BUILD_DIR = ./build
KRN_ENTRY_POINT = 0xc0100000
AS = nasm
CC = gcc
LD = ld
INCLUDE = -I lib -I lib/kernel -I . -I lib/shared -I kernel
ASFLAGS = -I lib/asm/include -f elf
CFLAGS = -Wall -Wmissing-prototypes -Wstrict-prototypes -D_KDBG -Werror -fno-stack-protector -fno-builtin -fdiagnostics-color -c -m32 $(INCLUDE)
LDFLAGS = -Ttext $(KRN_ENTRY_POINT) -m elf_i386 -e KrnlEntry -Map $(BUILD_DIR)/kernel.map
OBJS = $(BUILD_DIR)/kernel.o $(BUILD_DIR)/clock_irq.o $(BUILD_DIR)/krnlio_asm.o $(BUILD_DIR)/interrupt.o $(BUILD_DIR)/krnlio.o \
       $(BUILD_DIR)/init.o $(BUILD_DIR)/kernel_asm.o $(BUILD_DIR)/krnldbg.o $(BUILD_DIR)/libc.o $(BUILD_DIR)/bitmap.o \
       $(BUILD_DIR)/memory.o $(BUILD_DIR)/sched_asm.o $(BUILD_DIR)/sched.o $(BUILD_DIR)/sync.o $(BUILD_DIR)/list.o \
       $(BUILD_DIR)/console.o $(BUILD_DIR)/keyboard.o $(BUILD_DIR)/iocbuf.o
SHARED_HEADERS = lib/shared/*.h
KERNEL_HEADERS = kernel/*.h

$(BUILD_DIR)/krnlio_asm.o: lib/kernel/krnlio.asm
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/krnlio.o: lib/kernel/krnlio.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sched_asm.o: kernel/task/switch.asm
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/kernel_asm.o: kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: kernel/structs/bitmap.c kernel/structs/bitmap.h $(KERNEL_HEADERS) $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/kernel.o: kernel/kernel.c kernel/init.h lib/kernel/krnlio.h $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/clock_irq.o: lib/device/clock_irq.c lib/asm/i386asm.h lib/device/clock_irq.h lib/kernel/krnlio.h $(KERNEL_HEADERS) $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/interrupt.c kernel/interrupt.h lib/asm/i386asm.h lib/asm/i386def.h lib/kernel/krnlio.h $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/init.o: kernel/init.c kernel/init.h lib/device/clock_irq.h lib/kernel/krnlio.h $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/krnldbg.o: kernel/krnldbg.c kernel/krnldbg.h kernel/interrupt.h lib/kernel/krnlio.h $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/libc.o: lib/shared/libc/*.c $(SHARED_HEADERS)
	$(CC) $(CFLAGS) lib/shared/libc/*.c -o $@

$(BUILD_DIR)/memory.o: kernel/memory.c kernel/memory.h $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sched.o: kernel/task/sched.c kernel/task/sched.h  kernel/memory.h $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/sync.o: kernel/task/sync.c kernel/task/sync.h kernel/task/sched.h  kernel/memory.h $(KERNEL_HEADERS) $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/list.o: kernel/structs/list.c kernel/structs/list.h $(KERNEL_HEADERS) $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/console.o: lib/device/console.c lib/device/console.h $(KERNEL_HEADERS) $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/keyboard.o: lib/device/keyboard.c lib/device/keyboard.h lib/device/iocbuf.h $(KERNEL_HEADERS) $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/iocbuf.o: lib/device/iocbuf.c lib/device/iocbuf.h $(KERNEL_HEADERS) $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/kernel.bin: $(OBJS) 
	$(LD) $(LDFLAGS) $^ -o $@

.PHONY: make_dir hd build all rebuild

make_dir:
	@/bin/bash -c "if [[ ! -d $(BUILD_DIR) ]];then mkdir $(BUILD_DIR); fi"

hd:
	@echo "Writing kernel to disk..."
	@dd if=build/kernel.bin of=disk.img bs=512 count=200 seek=9 conv=notrunc

clean:
	cd $(BUILD_DIR); rm -f ./*

build: $(BUILD_DIR)/kernel.bin
	
all: make_dir build hd

rebuild: clean all