# == Himu Operating System Makefile
# This Makefile provides:
# 1. An EFI binary for booting the Himu OS.
# 2. The kernel and other components.
# 3. Runs the OS in QEMU with OVMF support.
# 4. Other utilities like clean and install.
# Copyright (C) 2024-2026 HimuOS, ONLY for educational purposes.

# ==============================================================================
# Configuration
# ==============================================================================

# Cross compiler for x86_64 EFI applications
CC_EFI      := x86_64-w64-mingw32-gcc
CFLAGS_EFI  := -Wall -Wextra -nostdlib -fno-builtin -nostartfiles -nodefaultlibs \
               -nostdinc -ffreestanding -c -mavx2 -g \
               -Isrc -Isrc/include -Isrc/include/libc
LDFLAGS_EFI := -nostdlib -Wl,--subsystem,10 -e efi_main

# Kernel compiler and linker
CC          := gcc
LD          := ld
ASFLAGS     := -f elf64 -g -F dwarf

KRNL_VER_MAJOR  := 1
KRNL_VER_MINOR  := 0
KRNL_VER_PATCH  := 0
KRNL_BUILD_DATE := $(shell date +'%y%m%d')
KRNL_VERSTR     := "$(KRNL_VER_MAJOR).$(KRNL_VER_MINOR).$(KRNL_VER_PATCH) $(KRNL_BUILD_DATE)"
KRNL_ENTRY_POINT := 0xFFFF800000000000

HO_DEBUG_BUILD ?= 1
HO_ENABLE_TIMESTAMP_LOG ?= $(HO_DEBUG_BUILD)

CFLAGS := -Wall -Wextra -Wmissing-prototypes -Wstrict-prototypes -Werror \
          -fno-stack-protector -nostdlib -fno-builtin -nostartfiles \
          -nodefaultlibs -nostdinc -ffreestanding -fdiagnostics-color \
          -c -m64 -g -mcmodel=large \
          -Isrc -Isrc/include -Isrc/include/libc \
          -DKRNL_VERSTR=\"$(KRNL_VERSTR)\" \
          -D__HO_DEBUG_BUILD__=$(HO_DEBUG_BUILD) \
          -DHO_ENABLE_TIMESTAMP_LOG=$(HO_ENABLE_TIMESTAMP_LOG)

LDFLAGS := -T himuos.ld -nostdlib -static -e kmain -Map=build/kernel/bin/kernel.map

# Output directories (explicit per-target)
EFI_OBJDIR    := build/efi/obj
EFI_BINDIR    := build/efi/bin
KRN_OBJDIR    := build/kernel/obj
KRN_BINDIR    := build/kernel/bin

# ==============================================================================
# Source Files
# ==============================================================================

# ------------------------------------------------------------------------------
# LIBC Sources (Full library for kernel)
# ------------------------------------------------------------------------------
SRCS_LIBC := \
    src/libc/string/memcpy.c   \
    src/libc/string/memmove.c  \
    src/libc/string/memset.c   \
    src/libc/string/memcmp.c   \
    src/libc/string/strcmp.c   \
    src/libc/string/strcpy.c   \
    src/libc/string/strlen.c   \
    src/libc/wchar/wstrcmp.c   \
    src/libc/stdlib/hostdlib.c

# ------------------------------------------------------------------------------
# LIBC Sources (Minimal subset for bootloader)
# ------------------------------------------------------------------------------
SRCS_LIBC_BOOT := \
    src/libc/string/memcpy.c   \
    src/libc/string/memmove.c  \
    src/libc/string/memset.c   \
    src/libc/string/memcmp.c   \
    src/libc/string/strcmp.c   \
    src/libc/string/strcpy.c   \
    src/libc/string/strlen.c   \
    src/libc/wchar/wstrcmp.c   \
    src/libc/stdlib/hostdlib.c

# ------------------------------------------------------------------------------
# Common ELF Sources
# ------------------------------------------------------------------------------
SRCS_ELF := \
    src/lib/elf/elf.c

# ------------------------------------------------------------------------------
# EFI Bootloader Sources
# ------------------------------------------------------------------------------
SRCS_EFI_BOOT := \
    src/boot/v2/blmm.c        \
    src/boot/v2/bootloader.c  \
    src/boot/v2/ho_balloc.c   \
    src/boot/v2/efi_main.c    \
    src/boot/v2/io.c          \
    src/arch/amd64/pm.c

# EFI target: bootloader + minimal libc + elf
SRCS_EFI_ALL := $(SRCS_EFI_BOOT) $(SRCS_LIBC_BOOT) $(SRCS_ELF)
OBJS_EFI_C   := $(patsubst src/%.c,$(EFI_OBJDIR)/%.o,$(SRCS_EFI_ALL))
TARGET_EFI   := $(EFI_BINDIR)/main.efi

# ------------------------------------------------------------------------------
# Kernel Sources
# ------------------------------------------------------------------------------
SRCS_KERNEL_C := \
    src/kernel/hoentry.c                                \
    src/kernel/init.c                                   \
    src/kernel/hodbg.c                                  \
    src/kernel/ke/console/console.c                     \
    src/kernel/ke/console/console_device.c              \
    src/kernel/ke/console/sinks/gfx_console_sink.c      \
    src/kernel/ke/console/sinks/serial_console_sink.c   \
    src/kernel/ke/console/sinks/mux_console_sink.c      \
    src/kernel/ke/time/time_source.c                    \
    src/kernel/ke/time/sinks/tsc_sink.c                 \
    src/kernel/ke/time/sinks/hpet_sink.c                \
    src/kernel/ke/log/log.c                             \
    src/arch/arch.c                                     \
    src/arch/amd64/idt.c                                \
    src/arch/amd64/cpu.c                                \
    src/arch/amd64/pm.c                                 \
    src/drivers/time/tsc_driver.c                       \
    src/drivers/time/hpet_driver.c                      \
    src/drivers/video/video_driver.c                    \
    src/drivers/video/efi/video_efi.c                   \
    src/drivers/serial/serial.c                         \
    src/lib/tui/bitmap_font.c                           \
    src/assets/fonts/font8x16.c

SRCS_KERNEL_ASM := \
    src/arch/amd64/intr_stub.asm

# Kernel target: kernel sources + full libc + elf
SRCS_KERNEL_ALL := $(SRCS_KERNEL_C) $(SRCS_LIBC) $(SRCS_ELF) $(SRCS_KERNEL_ASM)

OBJS_KERNEL_C   := $(patsubst src/%.c,$(KRN_OBJDIR)/%.o,$(filter %.c,$(SRCS_KERNEL_ALL)))
OBJS_KERNEL_ASM := $(patsubst src/%.asm,$(KRN_OBJDIR)/%.o,$(filter %.asm,$(SRCS_KERNEL_ALL)))
OBJS_KERNEL     := $(OBJS_KERNEL_C) $(OBJS_KERNEL_ASM)

TARGET_KERNEL := $(KRN_BINDIR)/kernel.bin

.PHONY: all clean run efi install clean_code vmware_img kernel debug run_iso

all: efi kernel

efi: $(TARGET_EFI)
	@echo "EFI build complete: $<"

$(TARGET_EFI): $(OBJS_EFI_C)
	@mkdir -p $(dir $@)
	@echo "(PE) LD $@"
	@$(CC_EFI) -o $@ $^ $(LDFLAGS_EFI)

$(EFI_OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	@echo "(PE) CC $<"
	@$(CC_EFI) $(CFLAGS_EFI) -o $@ $<

kernel: $(TARGET_KERNEL)
	@echo "Kernel build complete: $<"

$(TARGET_KERNEL): $(OBJS_KERNEL)
	@mkdir -p $(dir $@)
	@echo "LD $@"
	@$(LD) -o $@ $^ $(LDFLAGS)

$(KRN_OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -o $@ $<

$(KRN_OBJDIR)/%.o: src/%.asm
	@mkdir -p $(dir $@)
	@echo "ASM $<"
	@nasm $(ASFLAGS) -o $@ $<

# ------------------------------------------------------------------------------
# Runtime artifacts for QEMU (vvfat): only refresh when binaries change
# ------------------------------------------------------------------------------
ESP_BOOT_EFI   := esp/EFI/BOOT/BOOTX64.efi
ESP_KERNEL_BIN := esp/kernel.bin

copy: $(ESP_BOOT_EFI) $(ESP_KERNEL_BIN)
	@echo "Copied EFI and kernel to esp/ directory."

$(ESP_BOOT_EFI): $(TARGET_EFI)
	@mkdir -p $(dir $@)
	@cp $< $@

$(ESP_KERNEL_BIN): $(TARGET_KERNEL)
	@mkdir -p $(dir $@)
	@cp $< $@

run: $(ESP_BOOT_EFI) $(ESP_KERNEL_BIN)
	@echo "Starting VM with EFI..."
	@sudo qemu-system-x86_64 \
		-m 512M \
		-bios /usr/share/OVMF/OVMF_CODE.fd \
		-net none \
		-cpu host,+invtsc \
		-enable-kvm \
		-drive file=fat:rw:esp,index=0,format=vvfat \
		-serial stdio
		
debug: $(ESP_BOOT_EFI) $(ESP_KERNEL_BIN)
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

# ==============================================================================
# Dependency handling
# Generate and include .d files for C sources to track headers accurately.
# This improves incremental rebuild correctness and avoids over/under-building.
# ==============================================================================

# Enable depfile generation for both EFI and Kernel C compilations
CFLAGS_EFI += -MMD -MP
CFLAGS     += -MMD -MP

# Include auto-generated dependency files (if they exist)
-include $(OBJS_EFI_C:.o=.d)
-include $(OBJS_KERNEL_C:.o=.d)
