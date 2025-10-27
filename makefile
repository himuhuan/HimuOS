# == Himu Operating System Makefile
# This Makefile provides:
# 1. An EFI binary for booting the Himu OS.
# 2. The kernel and other components.
# 3. Runs the OS in QEMU with OVMF support.
# 4. Other utilities like clean and install.
# Copyright (C) 2024-2026 HimuOS, ONLY for educational purposes.

# == Configuration

# Cross compiler for x86_64 EFI applications
CC_EFI = x86_64-w64-mingw32-gcc
CFLAGS_EFI  := -Wall -Wextra -nostdlib -fno-builtin -nostartfiles -nodefaultlibs -nostdinc -ffreestanding  \
	-c -mavx2 -g -Isrc -Isrc/include -Isrc/include/libc
LDFLAGS_EFI := -nostdlib -Wl,--subsystem,10 -e efi_main

# Kernel

KRNL_VER_MAJOR = 1
KRNL_VER_MINOR = 0
KRNL_VER_PATCH = 0
KRNL_BUILD_DATE := $(shell date +'%y%m%d')
KRNL_VERSTR := "$(KRNL_VER_MAJOR).$(KRNL_VER_MINOR).$(KRNL_VER_PATCH) $(KRNL_BUILD_DATE)"

KRNL_ENTRY_POINT = 0xFFFF800000000000
CC = gcc
LD = ld
CFLAGS = -Wall -Wextra -Wmissing-prototypes -Wstrict-prototypes -Werror -fno-stack-protector \
	-nostdlib -fno-builtin -nostartfiles -nodefaultlibs -nostdinc -ffreestanding -fdiagnostics-color \
	-c -m64 -g -mcmodel=large -Isrc -Isrc/include -Isrc/include/libc \
	-DKRNL_VERSTR=\"$(KRNL_VERSTR)\" -D__HO_DEBUG_BUILD__=1
LDFLAGS = -T himuos.ld -nostdlib -static -e kmain -Map=build/kernel/bin/kernel.map
# == Sources and Targets

# Sources may be used in multiple targets: UEFI, kernel, etc.
SRCS_SHARED := $(shell find src/libc -name '*.c') \
$(shell find src/common/elf -name '*.c')

SRCS_EFI_ONLY := src/boot/efi/efi_main.c src/boot/efi/efi.c src/boot/efi/shell.c src/boot/efi/alloc.c src/boot/efi/bootloader.c \
	src/boot/efi/io.c src/kernel/mm/mm_efi.c src/arch/amd64/pm.c 
	
SRCS_EFI_ALL := $(SRCS_EFI_ONLY) $(SRCS_SHARED)
OBJS_EFI     := $(patsubst src/%.c,build/efi/obj/%.o,$(SRCS_EFI_ALL))
TARGET_EFI   := build/efi/bin/main.efi

SRCS_KERNEL_ONLY := src/kernel/hoentry.c \
	src/drivers/video/video_driver.c \
	src/drivers/video/efi/video_efi.c \
	src/drivers/serial/serial.c \
	src/lib/tui/bitmap_font.c \
	src/kernel/init.c \
	src/kernel/hodbg.c \
	src/kernel/console/console.c \
	src/kernel/console/console_device.c \
	src/kernel/console/sinks/gfx_console_sink.c \
	src/kernel/console/sinks/serial_console_sink.c \
	src/kernel/console/sinks/mux_console_sink.c \
	src/assets/fonts/font8x16.c
	
SRCS_KERNEL_ALL  := $(SRCS_KERNEL_ONLY) $(SRCS_SHARED)
OBJS_KERNEL     := $(patsubst src/%.c,build/kernel/obj/%.o,$(SRCS_KERNEL_ALL))
TARGET_KERNEL   := build/kernel/bin/kernel.bin

.PHONY: all clean run efi install clean_code vmware_img kernel debug iso run_iso

all: efi kernel

efi: $(TARGET_EFI)
	@echo "EFI build complete: $<"

$(TARGET_EFI): $(OBJS_EFI)
	@mkdir -p $(dir $@)
	@echo "(PE) LD $@"
	@$(CC_EFI) -o $@ $^ $(LDFLAGS_EFI)

build/efi/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	@echo "(PE) CC $<"
	@$(CC_EFI) $(CFLAGS_EFI) -o $@ $<

kernel: $(TARGET_KERNEL)
	@echo "Kernel build complete: $<"

$(TARGET_KERNEL): $(OBJS_KERNEL)
	@mkdir -p $(dir $@)
	@echo "LD $@"
	@$(LD) -o $@ $^ $(LDFLAGS)

build/kernel/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -o $@ $<

copy: efi kernel
	@mkdir -p esp/EFI/BOOT
	@cp $(TARGET_EFI) esp/EFI/BOOT/BOOTX64.efi
	@cp $(TARGET_KERNEL) esp/kernel.bin
	@echo "Copied EFI and kernel to esp/ directory."

run: copy
	@echo "Starting VM with EFI..."
	@qemu-system-x86_64 \
		-m 512M \
		-bios /usr/share/OVMF/OVMF_CODE.fd \
		-net none \
		-drive file=fat:rw:esp,index=0,format=vvfat \
		-serial stdio
		
debug: copy
	@echo "Starting VM with EFI and GDB server..."
	@qemu-system-x86_64 \
		-m 512M \
		-bios /usr/share/OVMF/OVMF_CODE.fd \
		-net none \
		-drive file=fat:rw:esp,index=0,format=vvfat \
		-serial stdio \
		-s -S

clean:
	rm -rf build esp/EFI/BOOT/BOOTX64.efi
	rm -f esp/kernel.bin
	rm -f himu_os.img
	rm -rf build/iso

clean_code:
	@echo "Formatting source code..."
	@find src -type f \( -name "*.c" -o -name "*.h" \) -print0 | xargs -0 clang-format -i
	@echo "Code formatting complete."

ISO_NAME := himu_os.iso
ISO_ROOT := build/iso
UEFI_IMG := build/uefi.img

iso: all copy
	@echo "Creating Hybrid UEFI Bootable ISO... 🚀"
	@rm -rf $(ISO_ROOT) $(ISO_NAME) $(UEFI_IMG)
	@mkdir -p $(ISO_ROOT)
	@echo "Creating FAT32 boot image..."
	@dd if=/dev/zero of=$(UEFI_IMG) bs=1M count=64 > /dev/null 2>&1
	@mformat -i $(UEFI_IMG) -F ::
	@mmd -i $(UEFI_IMG) ::/EFI
	@mmd -i $(UEFI_IMG) ::/EFI/BOOT
	@mcopy -i $(UEFI_IMG) $(TARGET_EFI) ::/EFI/BOOT/BOOTX64.EFI
	@mcopy -i $(UEFI_IMG) $(TARGET_KERNEL) ::/kernel.bin
	@cp $(UEFI_IMG) $(ISO_ROOT)/
	@cp $(TARGET_KERNEL) $(ISO_ROOT)/kernel.bin
	@echo "Generating Hybrid ISO image: $(ISO_NAME)"
	@genisoimage -o $(ISO_NAME) \
		-no-emul-boot \
		-eltorito-boot $(notdir $(UEFI_IMG)) \
		-J -R -l $(ISO_ROOT)
	@echo ""
	@echo "✅ Hybrid Bootable ISO is ready: $(ISO_NAME)"

run_iso: $(ISO_NAME)
	qemu-system-x86_64 \
    -m 512M \
    -bios /usr/share/OVMF/OVMF_CODE.fd \
    -net none \
    -cdrom himu_os.iso \
    -serial stdio