# HIMU OPERATING SYSTEM
# Copyright (C) 2024 HimuOS Project, all rights reserved.

BUILD_DIR = ./build
KRN_ENTRY_POINT = 0xc0100000
AS = nasm
CC = gcc
LD = ld
INCLUDE = -I lib -I . -I lib/shared
ASFLAGS = -I lib/asm/include -f elf
CFLAGS = -Wall -Wmissing-prototypes -Wstrict-prototypes -D_KDBG -Werror -fno-stack-protector -fno-builtin -fdiagnostics-color -c -m32 $(INCLUDE)
LDFLAGS = -Ttext $(KRN_ENTRY_POINT) -m elf_i386 -e KrnlEntry -Map $(BUILD_DIR)/kernel.map
OBJS = $(BUILD_DIR)/kernel.o $(BUILD_DIR)/clock_irq.o $(BUILD_DIR)/krnlio_asm.o $(BUILD_DIR)/interrupt.o $(BUILD_DIR)/krnlio.o \
       $(BUILD_DIR)/init.o $(BUILD_DIR)/kernel_asm.o $(BUILD_DIR)/krnldbg.o $(BUILD_DIR)/libc.o $(BUILD_DIR)/bitmap.o \
       $(BUILD_DIR)/memory.o
SHARED_HEADERS = lib/shared/*.h
KERNEL_HEADERS = kernel/*.h

$(BUILD_DIR)/krnlio_asm.o: lib/kernel/krnlio.asm
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/krnlio.o: lib/kernel/krnlio.c
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/kernel_asm.o: kernel/kernel.S
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/bitmap.o: kernel/structs/bitmap.c kernel/structs/bitmap.h $(KERNEL_HEADERS) $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/kernel.o: kernel/kernel.c kernel/init.h lib/kernel/krnlio.h $(SHARED_HEADERS)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/clock_irq.o: kernel/i8253.c lib/asm/i386asm.h lib/device/clock_irq.h lib/kernel/krnlio.h $(SHARED_HEADERS)
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