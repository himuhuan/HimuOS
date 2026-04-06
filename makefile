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
HO_ENABLE_NULL_DETECTION ?= 1

CC_EFI      := x86_64-w64-mingw32-gcc
CFLAGS_EFI  := -Wall -Wextra -nostdlib -fno-builtin -nostartfiles -nodefaultlibs \
               -nostdinc -ffreestanding -c -mavx2 -g \
			   -Isrc -Isrc/include -Isrc/include/libc \
			   -DHO_ENABLE_NULL_DETECTION=$(HO_ENABLE_NULL_DETECTION)
LDFLAGS_EFI := -nostdlib -Wl,--subsystem,10 -e efi_main

# Kernel compiler and linker
CC          := gcc
LD          := ld
OBJCOPY     := objcopy
ASFLAGS     := -f elf64 -g -F dwarf

BUILD_FLAVOR ?=

DEFAULT_INTERACTIVE_BUILD_FLAVOR ?= default-demo_shell
DEFAULT_INTERACTIVE_PROFILE_NAME ?= demo_shell
DEFAULT_INTERACTIVE_PROFILE_DEFINE ?= HO_DEMO_TEST_DEMO_SHELL
INTERACTIVE_ENTRY_GOALS := copy run debug iso run_iso
QUIET_INTERACTIVE_LOG_GOALS := run iso run_iso

ifeq ($(strip $(BUILD_FLAVOR)$(HO_DEMO_TEST_NAME)$(HO_DEMO_TEST_DEFINE)),)
ifneq ($(filter $(INTERACTIVE_ENTRY_GOALS),$(MAKECMDGOALS)),)
BUILD_FLAVOR := $(DEFAULT_INTERACTIVE_BUILD_FLAVOR)
HO_DEMO_TEST_NAME := $(DEFAULT_INTERACTIVE_PROFILE_NAME)
HO_DEMO_TEST_DEFINE := $(DEFAULT_INTERACTIVE_PROFILE_DEFINE)
endif
endif

KRNL_VER_MAJOR  := 1
KRNL_VER_MINOR  := 0
KRNL_VER_PATCH  := 0
KRNL_BUILD_DATE := $(shell date +'%y%m%d')
KRNL_VERSTR     := "$(KRNL_VER_MAJOR).$(KRNL_VER_MINOR).$(KRNL_VER_PATCH) $(KRNL_BUILD_DATE)"
KRNL_ENTRY_POINT := 0xFFFF800000000000
comma := ,

HO_DEBUG_BUILD ?= 1
HO_ENABLE_TIMESTAMP_LOG ?= $(HO_DEBUG_BUILD)
SUDO ?= sudo
QEMU_ACCEL_MODE ?= host
QEMU_DISPLAY ?= gtk
QEMU_MONITOR_SOCKET ?=

ifeq ($(origin HO_LOG_MIN_LEVEL), undefined)
ifneq ($(filter $(QUIET_INTERACTIVE_LOG_GOALS),$(MAKECMDGOALS)),)
ifeq ($(QEMU_DISPLAY),gtk)
HO_LOG_MIN_LEVEL := KLOG_LEVEL_WARNING
endif
endif
endif

HO_LOG_MIN_LEVEL ?= KLOG_LEVEL_DEBUG

ifeq ($(QEMU_ACCEL_MODE),host)
QEMU_ACCEL_ARGS ?= -enable-kvm
QEMU_CPU_FLAGS ?= host,+invtsc
else ifeq ($(QEMU_ACCEL_MODE),tcg)
QEMU_ACCEL_ARGS ?= -accel tcg
QEMU_CPU_FLAGS ?= max
else ifeq ($(QEMU_ACCEL_MODE),custom)
QEMU_CPU_FLAGS ?= max
else
$(error Unknown QEMU_ACCEL_MODE '$(QEMU_ACCEL_MODE)'. Use host, tcg, or custom.)
endif

ifeq ($(strip $(SUDO_PASSWORD)),)
SUDO_RUN := $(SUDO) -p ''
else
SUDO_RUN := printf '%s\n' "$(SUDO_PASSWORD)" | $(SUDO) -S -p ''
endif

# UEFI firmware path for QEMU (override with: make run OVMF_CODE=/path/to/OVMF_CODE.fd)
OVMF_CODE ?= $(shell \
	for p in \
		/usr/share/edk2/x64/OVMF.4m.fd \
		/usr/share/OVMF/OVMF.fd \
		/usr/share/qemu/OVMF.fd \
		/usr/share/OVMF/OVMF_CODE.fd \
		/usr/share/edk2/x64/OVMF_CODE.4m.fd \
		/usr/share/edk2/x64/OVMF_CODE.fd \
		/usr/share/edk2/x64/OVMF_CODE.secboot.4m.fd; do \
		[ -r "$$p" ] && { echo "$$p"; break; }; \
	done)

CFLAGS := -Wall -Wextra -Wmissing-prototypes -Wstrict-prototypes -Werror \
          -fno-stack-protector -nostdlib -fno-builtin -nostartfiles \
          -nodefaultlibs -nostdinc -ffreestanding -fdiagnostics-color \
          -c -m64 -g -mcmodel=large -mno-red-zone -mgeneral-regs-only \
          -Isrc -Isrc/include -Isrc/include/libc \
          -DKRNL_VERSTR=\"$(KRNL_VERSTR)\" \
          -D__HO_DEBUG_BUILD__=$(HO_DEBUG_BUILD) \
		  -DHO_LOG_MIN_LEVEL=$(HO_LOG_MIN_LEVEL) \
		  -DHO_ENABLE_TIMESTAMP_LOG=$(HO_ENABLE_TIMESTAMP_LOG) \
		  -DHO_ENABLE_NULL_DETECTION=$(HO_ENABLE_NULL_DETECTION)

ifneq ($(strip $(HO_DEMO_TEST_DEFINE)),)
CFLAGS += -DHO_DEMO_TEST_SELECTION=$(HO_DEMO_TEST_DEFINE) \
          -DHO_DEMO_TEST_SELECTION_NAME=\"$(HO_DEMO_TEST_NAME)\"
endif

LDFLAGS = -T himuos.ld -nostdlib -static -e kmain -Map=$(KRN_BINDIR)/kernel.map

USER_CFLAGS := -Wall -Wextra -Werror \
               -fno-stack-protector -fno-builtin -fno-pie \
               -fno-asynchronous-unwind-tables -fno-unwind-tables \
               -nostdlib -nostartfiles -nodefaultlibs -nostdinc \
               -ffreestanding -fdiagnostics-color -c -m64 -g \
               -Isrc -Isrc/include -Isrc/include/libc -Isrc/user
USER_LDFLAGS := -nostdlib -static --build-id=none -z max-page-size=0x1000

ifneq ($(strip $(HO_DEMO_TEST_DEFINE)),)
USER_CFLAGS += -D$(HO_DEMO_TEST_DEFINE)=1 \
               -DHO_DEMO_TEST_SELECTION_NAME=\"$(HO_DEMO_TEST_NAME)\"
endif

# Output directories (explicit per-target)
EFI_OBJDIR    := build/efi/obj
EFI_BINDIR    := build/efi/bin
KRN_BUILDROOT := build/kernel$(if $(strip $(BUILD_FLAVOR)),/$(BUILD_FLAVOR),)
KRN_OBJDIR    := $(KRN_BUILDROOT)/obj
KRN_BINDIR    := $(KRN_BUILDROOT)/bin
USR_BUILDROOT := build/user$(if $(strip $(BUILD_FLAVOR)),/$(BUILD_FLAVOR),)
USR_OBJDIR    := $(USR_BUILDROOT)/obj
USR_BINDIR    := $(USR_BUILDROOT)/bin

VALID_TEST_MODULES := schedule guard_wait owned_exit irql_wait irql_sleep irql_yield irql_exit pf_imported pf_guard pf_fixmap pf_heap kthread_pool_race user_hello user_caps user_dual user_input demo_shell list
TEST_MODULE_GOALS  := $(filter-out test,$(MAKECMDGOALS))
TEST_MODULE        := $(if $(strip $(TEST_MODULE_GOALS)),$(firstword $(TEST_MODULE_GOALS)),list)
TEST_BUILD_FLAVOR  := test-$(TEST_MODULE)

TEST_DEFINE_schedule := HO_DEMO_TEST_SCHEDULE
TEST_DEFINE_guard_wait := HO_DEMO_TEST_GUARD_WAIT
TEST_DEFINE_owned_exit := HO_DEMO_TEST_OWNED_EXIT
TEST_DEFINE_irql_wait := HO_DEMO_TEST_IRQL_WAIT
TEST_DEFINE_irql_sleep := HO_DEMO_TEST_IRQL_SLEEP
TEST_DEFINE_irql_yield := HO_DEMO_TEST_IRQL_YIELD
TEST_DEFINE_irql_exit := HO_DEMO_TEST_IRQL_EXIT
TEST_DEFINE_pf_imported := HO_DEMO_TEST_PF_IMPORTED
TEST_DEFINE_pf_guard := HO_DEMO_TEST_PF_GUARD
TEST_DEFINE_pf_fixmap := HO_DEMO_TEST_PF_FIXMAP
TEST_DEFINE_pf_heap := HO_DEMO_TEST_PF_HEAP
TEST_DEFINE_kthread_pool_race := HO_DEMO_TEST_KTHREAD_POOL_RACE
TEST_DEFINE_user_hello := HO_DEMO_TEST_USER_HELLO
TEST_DEFINE_user_caps := HO_DEMO_TEST_USER_CAPS
TEST_DEFINE_user_dual := HO_DEMO_TEST_USER_DUAL
TEST_DEFINE_user_input := HO_DEMO_TEST_USER_INPUT
TEST_DEFINE_demo_shell := HO_DEMO_TEST_DEMO_SHELL

ifneq ($(filter test,$(MAKECMDGOALS)),)
ifneq ($(words $(TEST_MODULE_GOALS)),0)
ifneq ($(words $(TEST_MODULE_GOALS)),1)
$(error Usage: make test <module>. Available modules: schedule, guard_wait, owned_exit, irql_wait, irql_sleep, irql_yield, irql_exit, pf_imported, pf_guard, pf_fixmap, pf_heap, kthread_pool_race, user_hello, user_caps, user_dual, user_input, demo_shell. Use `make test` or `make test list` to inspect supported modules)
endif
ifneq ($(filter $(TEST_MODULE),$(VALID_TEST_MODULES)), $(TEST_MODULE))
$(error Unknown test module '$(TEST_MODULE)'. Available modules: schedule, guard_wait, owned_exit, irql_wait, irql_sleep, irql_yield, irql_exit, pf_imported, pf_guard, pf_fixmap, pf_heap, kthread_pool_race, user_hello, user_caps, user_dual, user_input, demo_shell. Use `make test list` to inspect supported modules)
endif
endif
endif

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
    src/boot/v2/blmm/blmm.c   \
    src/boot/v2/blmm/acpi.c   \
    src/boot/v2/blmm/hhdm.c   \
    src/boot/v2/blmm/capsule.c \
    src/boot/v2/blmm/pagetable.c \
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
    src/kernel/init/init.c                              \
    src/kernel/hodbg.c                                  \
    src/kernel/demo/demo.c                              \
    src/kernel/demo/event.c                             \
    src/kernel/demo/guard.c                             \
    src/kernel/demo/irql.c                              \
    src/kernel/demo/mutex.c                             \
    src/kernel/demo/pagefault.c                         \
	src/kernel/demo/kthread_pool_race.c                 \
	src/kernel/demo/semaphore.c                         \
    src/kernel/demo/thread.c                            \
    src/kernel/demo/user_counter_artifact_bridge.c      \
    src/kernel/demo/demo_shell.c                        \
    src/kernel/demo/demo_shell_runtime.c                \
    src/kernel/demo/hsh_artifact_bridge.c               \
    src/kernel/demo/calc_artifact_bridge.c              \
    src/kernel/demo/tick1s_artifact_bridge.c            \
    src/kernel/demo/user_hello_artifact_bridge.c        \
	src/kernel/demo/user_hello.c                        \
    src/kernel/demo/user_dual.c                         \
	src/kernel/demo/user_caps.c                         \
    src/kernel/demo/user_input.c                        \
    src/kernel/init/cpu.c                               \
    src/kernel/init/font.c                              \
    src/kernel/init/hhdm.c                              \
    src/kernel/ke/critical_section.c                    \
    src/kernel/ke/irql.c                                \
    src/kernel/ke/console/console.c                     \
    src/kernel/ke/console/console_device.c              \
    src/kernel/ke/console/sinks/gfx_console_sink.c      \
    src/kernel/ke/console/sinks/serial_console_sink.c   \
    src/kernel/ke/console/sinks/mux_console_sink.c      \
    src/kernel/ke/time/time_source.c                    \
    src/kernel/ke/time/clock_event.c                    \
    src/kernel/ke/time/sinks/tsc_sink.c                 \
    src/kernel/ke/time/sinks/pmtimer_sink.c             \
    src/kernel/ke/time/sinks/hpet_sink.c                \
    src/kernel/ke/time/sinks/lapic_clockevent_sink.c    \
    src/kernel/ke/log/log.c                             \
    src/kernel/ke/sysinfo/sysinfo.c                     \
    src/kernel/ke/sysinfo/cpu.c                         \
    src/kernel/ke/sysinfo/memory.c                      \
    src/kernel/ke/sysinfo/scheduler.c                   \
    src/kernel/ke/sysinfo/tables.c                      \
    src/kernel/ke/sysinfo/time.c                        \
    src/kernel/ke/pmm/pmm_device.c                      \
    src/kernel/ke/pmm/bitmap_sink.c                     \
    src/kernel/ke/pmm/pmm_boot_init.c                   \
    src/kernel/ke/mm/address_space.c                    \
    src/kernel/ke/mm/kva.c                              \
    src/kernel/ke/mm/allocator.c                        \
    src/kernel/ke/mm/pool.c                             \
	src/kernel/ke/bootstrap_callbacks.c                 \
	src/kernel/ke/user_bootstrap.c                      \
	src/kernel/ke/user_bootstrap_syscall.c              \
    src/kernel/ex/ex_bootstrap.c                        \
	src/kernel/ex/ex_bootstrap_adapter.c                \
    src/kernel/ke/input/input.c                         \
    src/kernel/ke/input/sinks/ps2_keyboard_sink.c       \
    src/kernel/ke/thread/kthread.c                      \
    src/kernel/ke/thread/scheduler/scheduler.c          \
    src/kernel/ke/thread/scheduler/wait.c               \
    src/kernel/ke/thread/scheduler/sync.c               \
    src/kernel/ke/thread/scheduler/timer.c              \
    src/kernel/ke/thread/scheduler/diag.c               \
    src/arch/arch.c                                     \
    src/arch/amd64/idt.c                                \
    src/arch/amd64/cpu.c                                \
    src/arch/amd64/pm.c                                 \
    src/drivers/time/tsc_driver.c                       \
    src/drivers/time/pmtimer_driver.c                   \
    src/drivers/time/hpet_driver.c                      \
    src/drivers/time/lapic_timer_driver.c               \
    src/drivers/input/ps2_keyboard_driver.c             \
    src/drivers/video/video_driver.c                    \
    src/drivers/video/efi/video_efi.c                   \
    src/drivers/serial/serial.c                         \
    src/lib/tui/bitmap_font.c                           \
    src/lib/common/linked_list.c                        \
    src/assets/fonts/font8x16.c

SRCS_KERNEL_ASM := \
    src/arch/amd64/intr_stub.asm \
	src/arch/amd64/context_switch.asm \
	src/arch/amd64/user_bootstrap.asm

# Kernel target: kernel sources + full libc + elf
SRCS_KERNEL_ALL := $(SRCS_KERNEL_C) $(SRCS_LIBC) $(SRCS_ELF) $(SRCS_KERNEL_ASM)

OBJS_KERNEL_C   := $(patsubst src/%.c,$(KRN_OBJDIR)/%.o,$(filter %.c,$(SRCS_KERNEL_ALL)))
OBJS_KERNEL_ASM := $(patsubst src/%.asm,$(KRN_OBJDIR)/%.o,$(filter %.asm,$(SRCS_KERNEL_ALL)))

TARGET_KERNEL := $(KRN_BINDIR)/kernel.bin

# ------------------------------------------------------------------------------
# Userspace bootstrap artifacts
# ------------------------------------------------------------------------------
SRCS_USER_HELLO_C := \
    src/user/user_hello/main.c

SRCS_USER_COUNTER_C := \
    src/user/user_counter/main.c

SRCS_USER_HSH_C := \
    src/user/hsh/main.c

SRCS_USER_CALC_C := \
    src/user/calc/main.c

SRCS_USER_TICK1S_C := \
    src/user/tick1s/main.c

SRCS_USER_COMMON_S := \
    src/user/crt0.S

OBJS_USER_HELLO_C := $(patsubst src/%.c,$(USR_OBJDIR)/%.o,$(SRCS_USER_HELLO_C))
OBJS_USER_COUNTER_C := $(patsubst src/%.c,$(USR_OBJDIR)/%.o,$(SRCS_USER_COUNTER_C))
OBJS_USER_HSH_C := $(patsubst src/%.c,$(USR_OBJDIR)/%.o,$(SRCS_USER_HSH_C))
OBJS_USER_CALC_C := $(patsubst src/%.c,$(USR_OBJDIR)/%.o,$(SRCS_USER_CALC_C))
OBJS_USER_TICK1S_C := $(patsubst src/%.c,$(USR_OBJDIR)/%.o,$(SRCS_USER_TICK1S_C))
OBJS_USER_HELLO_S := $(patsubst src/%.S,$(USR_OBJDIR)/%.o,$(SRCS_USER_COMMON_S))
OBJS_USER_HELLO   := $(OBJS_USER_HELLO_C) $(OBJS_USER_HELLO_S)
OBJS_USER_COUNTER := $(OBJS_USER_COUNTER_C) $(OBJS_USER_HELLO_S)
OBJS_USER_HSH     := $(OBJS_USER_HSH_C) $(OBJS_USER_HELLO_S)
OBJS_USER_CALC    := $(OBJS_USER_CALC_C) $(OBJS_USER_HELLO_S)
OBJS_USER_TICK1S  := $(OBJS_USER_TICK1S_C) $(OBJS_USER_HELLO_S)

TARGET_USER_HELLO_ELF       := $(USR_BINDIR)/user_hello.elf
TARGET_USER_HELLO_CODE_BIN  := $(USR_BINDIR)/user_hello.code.bin
TARGET_USER_HELLO_CONST_BIN := $(USR_BINDIR)/user_hello.const.bin
TARGET_USER_HELLO           := $(TARGET_USER_HELLO_ELF) $(TARGET_USER_HELLO_CODE_BIN) $(TARGET_USER_HELLO_CONST_BIN)

TARGET_USER_COUNTER_ELF       := $(USR_BINDIR)/user_counter.elf
TARGET_USER_COUNTER_CODE_BIN  := $(USR_BINDIR)/user_counter.code.bin
TARGET_USER_COUNTER_CONST_BIN := $(USR_BINDIR)/user_counter.const.bin
TARGET_USER_COUNTER           := $(TARGET_USER_COUNTER_ELF) $(TARGET_USER_COUNTER_CODE_BIN) $(TARGET_USER_COUNTER_CONST_BIN)

TARGET_USER_HSH_ELF       := $(USR_BINDIR)/hsh.elf
TARGET_USER_HSH_CODE_BIN  := $(USR_BINDIR)/hsh.code.bin
TARGET_USER_HSH_CONST_BIN := $(USR_BINDIR)/hsh.const.bin
TARGET_USER_HSH           := $(TARGET_USER_HSH_ELF) $(TARGET_USER_HSH_CODE_BIN) $(TARGET_USER_HSH_CONST_BIN)

TARGET_USER_CALC_ELF       := $(USR_BINDIR)/calc.elf
TARGET_USER_CALC_CODE_BIN  := $(USR_BINDIR)/calc.code.bin
TARGET_USER_CALC_CONST_BIN := $(USR_BINDIR)/calc.const.bin
TARGET_USER_CALC           := $(TARGET_USER_CALC_ELF) $(TARGET_USER_CALC_CODE_BIN) $(TARGET_USER_CALC_CONST_BIN)

TARGET_USER_TICK1S_ELF       := $(USR_BINDIR)/tick1s.elf
TARGET_USER_TICK1S_CODE_BIN  := $(USR_BINDIR)/tick1s.code.bin
TARGET_USER_TICK1S_CONST_BIN := $(USR_BINDIR)/tick1s.const.bin
TARGET_USER_TICK1S           := $(TARGET_USER_TICK1S_ELF) $(TARGET_USER_TICK1S_CODE_BIN) $(TARGET_USER_TICK1S_CONST_BIN)

path_to_symbol = $(subst -,_,$(subst .,_,$(subst /,_,$(1))))

TARGET_USER_HELLO_CODE_OBJ  := $(KRN_OBJDIR)/demo/user_hello.code.bin.o
TARGET_USER_HELLO_CONST_OBJ := $(KRN_OBJDIR)/demo/user_hello.const.bin.o
TARGET_USER_COUNTER_CODE_OBJ  := $(KRN_OBJDIR)/demo/user_counter.code.bin.o
TARGET_USER_COUNTER_CONST_OBJ := $(KRN_OBJDIR)/demo/user_counter.const.bin.o
TARGET_USER_HSH_CODE_OBJ      := $(KRN_OBJDIR)/demo/hsh.code.bin.o
TARGET_USER_HSH_CONST_OBJ     := $(KRN_OBJDIR)/demo/hsh.const.bin.o
TARGET_USER_CALC_CODE_OBJ     := $(KRN_OBJDIR)/demo/calc.code.bin.o
TARGET_USER_CALC_CONST_OBJ    := $(KRN_OBJDIR)/demo/calc.const.bin.o
TARGET_USER_TICK1S_CODE_OBJ   := $(KRN_OBJDIR)/demo/tick1s.code.bin.o
TARGET_USER_TICK1S_CONST_OBJ  := $(KRN_OBJDIR)/demo/tick1s.const.bin.o
OBJS_KERNEL_EMBEDDED          := $(TARGET_USER_HELLO_CODE_OBJ) $(TARGET_USER_HELLO_CONST_OBJ) \
                                 $(TARGET_USER_COUNTER_CODE_OBJ) $(TARGET_USER_COUNTER_CONST_OBJ) \
                                 $(TARGET_USER_HSH_CODE_OBJ) $(TARGET_USER_HSH_CONST_OBJ) \
                                 $(TARGET_USER_CALC_CODE_OBJ) $(TARGET_USER_CALC_CONST_OBJ) \
                                 $(TARGET_USER_TICK1S_CODE_OBJ) $(TARGET_USER_TICK1S_CONST_OBJ)
OBJS_KERNEL                 := $(OBJS_KERNEL_C) $(OBJS_KERNEL_ASM) $(OBJS_KERNEL_EMBEDDED)

USER_HELLO_CODE_BIN_SYMBOL_BASE  := _binary_$(call path_to_symbol,$(TARGET_USER_HELLO_CODE_BIN))
USER_HELLO_CONST_BIN_SYMBOL_BASE := _binary_$(call path_to_symbol,$(TARGET_USER_HELLO_CONST_BIN))
USER_COUNTER_CODE_BIN_SYMBOL_BASE  := _binary_$(call path_to_symbol,$(TARGET_USER_COUNTER_CODE_BIN))
USER_COUNTER_CONST_BIN_SYMBOL_BASE := _binary_$(call path_to_symbol,$(TARGET_USER_COUNTER_CONST_BIN))
USER_HSH_CODE_BIN_SYMBOL_BASE      := _binary_$(call path_to_symbol,$(TARGET_USER_HSH_CODE_BIN))
USER_HSH_CONST_BIN_SYMBOL_BASE     := _binary_$(call path_to_symbol,$(TARGET_USER_HSH_CONST_BIN))
USER_CALC_CODE_BIN_SYMBOL_BASE     := _binary_$(call path_to_symbol,$(TARGET_USER_CALC_CODE_BIN))
USER_CALC_CONST_BIN_SYMBOL_BASE    := _binary_$(call path_to_symbol,$(TARGET_USER_CALC_CONST_BIN))
USER_TICK1S_CODE_BIN_SYMBOL_BASE   := _binary_$(call path_to_symbol,$(TARGET_USER_TICK1S_CODE_BIN))
USER_TICK1S_CONST_BIN_SYMBOL_BASE  := _binary_$(call path_to_symbol,$(TARGET_USER_TICK1S_CONST_BIN))

.PHONY: all clean copy run efi install clean_code vmware_img kernel user debug run_iso test schedule user_hello user_caps user_dual user_input demo_shell list

all: efi kernel user

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

user: $(TARGET_USER_HELLO) $(TARGET_USER_COUNTER) $(TARGET_USER_HSH) $(TARGET_USER_CALC) $(TARGET_USER_TICK1S)
	@echo "User build complete: $(TARGET_USER_HELLO_ELF) $(TARGET_USER_COUNTER_ELF) $(TARGET_USER_HSH_ELF) $(TARGET_USER_CALC_ELF) $(TARGET_USER_TICK1S_ELF)"

$(TARGET_USER_HELLO_ELF): $(OBJS_USER_HELLO) src/user/user.ld
	@mkdir -p $(dir $@)
	@echo "USER LD $@"
	@$(LD) -o $@ $(OBJS_USER_HELLO) $(USER_LDFLAGS) -T src/user/user.ld -Map=$(USR_BINDIR)/user_hello.map

$(TARGET_USER_HELLO_CODE_BIN): $(TARGET_USER_HELLO_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.text $< $@

$(TARGET_USER_HELLO_CONST_BIN): $(TARGET_USER_HELLO_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.rodata $< $@

$(TARGET_USER_COUNTER_ELF): $(OBJS_USER_COUNTER) src/user/user.ld
	@mkdir -p $(dir $@)
	@echo "USER LD $@"
	@$(LD) -o $@ $(OBJS_USER_COUNTER) $(USER_LDFLAGS) -T src/user/user.ld -Map=$(USR_BINDIR)/user_counter.map

$(TARGET_USER_COUNTER_CODE_BIN): $(TARGET_USER_COUNTER_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.text $< $@

$(TARGET_USER_COUNTER_CONST_BIN): $(TARGET_USER_COUNTER_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.rodata $< $@

$(TARGET_USER_HSH_ELF): $(OBJS_USER_HSH) src/user/user.ld
	@mkdir -p $(dir $@)
	@echo "USER LD $@"
	@$(LD) -o $@ $(OBJS_USER_HSH) $(USER_LDFLAGS) -T src/user/user.ld -Map=$(USR_BINDIR)/hsh.map

$(TARGET_USER_HSH_CODE_BIN): $(TARGET_USER_HSH_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.text $< $@

$(TARGET_USER_HSH_CONST_BIN): $(TARGET_USER_HSH_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.rodata $< $@

$(TARGET_USER_CALC_ELF): $(OBJS_USER_CALC) src/user/user.ld
	@mkdir -p $(dir $@)
	@echo "USER LD $@"
	@$(LD) -o $@ $(OBJS_USER_CALC) $(USER_LDFLAGS) -T src/user/user.ld -Map=$(USR_BINDIR)/calc.map

$(TARGET_USER_CALC_CODE_BIN): $(TARGET_USER_CALC_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.text $< $@

$(TARGET_USER_CALC_CONST_BIN): $(TARGET_USER_CALC_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.rodata $< $@

$(TARGET_USER_TICK1S_ELF): $(OBJS_USER_TICK1S) src/user/user.ld
	@mkdir -p $(dir $@)
	@echo "USER LD $@"
	@$(LD) -o $@ $(OBJS_USER_TICK1S) $(USER_LDFLAGS) -T src/user/user.ld -Map=$(USR_BINDIR)/tick1s.map

$(TARGET_USER_TICK1S_CODE_BIN): $(TARGET_USER_TICK1S_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.text $< $@

$(TARGET_USER_TICK1S_CONST_BIN): $(TARGET_USER_TICK1S_ELF)
	@mkdir -p $(dir $@)
	@echo "USER OBJCOPY $@"
	@$(OBJCOPY) -O binary --only-section=.rodata $< $@

$(TARGET_USER_HELLO_CODE_OBJ): $(TARGET_USER_HELLO_CODE_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_HELLO_CODE_BIN_SYMBOL_BASE)_start=gKiUserHelloCodeBytesStart \
		--redefine-sym $(USER_HELLO_CODE_BIN_SYMBOL_BASE)_end=gKiUserHelloCodeBytesEnd \
		$@

$(TARGET_USER_HELLO_CONST_OBJ): $(TARGET_USER_HELLO_CONST_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_HELLO_CONST_BIN_SYMBOL_BASE)_start=gKiUserHelloConstBytesStart \
		--redefine-sym $(USER_HELLO_CONST_BIN_SYMBOL_BASE)_end=gKiUserHelloConstBytesEnd \
		$@

$(TARGET_USER_COUNTER_CODE_OBJ): $(TARGET_USER_COUNTER_CODE_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_COUNTER_CODE_BIN_SYMBOL_BASE)_start=gKiUserCounterCodeBytesStart \
		--redefine-sym $(USER_COUNTER_CODE_BIN_SYMBOL_BASE)_end=gKiUserCounterCodeBytesEnd \
		$@

$(TARGET_USER_COUNTER_CONST_OBJ): $(TARGET_USER_COUNTER_CONST_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_COUNTER_CONST_BIN_SYMBOL_BASE)_start=gKiUserCounterConstBytesStart \
		--redefine-sym $(USER_COUNTER_CONST_BIN_SYMBOL_BASE)_end=gKiUserCounterConstBytesEnd \
		$@

$(TARGET_USER_HSH_CODE_OBJ): $(TARGET_USER_HSH_CODE_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_HSH_CODE_BIN_SYMBOL_BASE)_start=gKiHshCodeBytesStart \
		--redefine-sym $(USER_HSH_CODE_BIN_SYMBOL_BASE)_end=gKiHshCodeBytesEnd \
		$@

$(TARGET_USER_HSH_CONST_OBJ): $(TARGET_USER_HSH_CONST_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_HSH_CONST_BIN_SYMBOL_BASE)_start=gKiHshConstBytesStart \
		--redefine-sym $(USER_HSH_CONST_BIN_SYMBOL_BASE)_end=gKiHshConstBytesEnd \
		$@

$(TARGET_USER_CALC_CODE_OBJ): $(TARGET_USER_CALC_CODE_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_CALC_CODE_BIN_SYMBOL_BASE)_start=gKiCalcCodeBytesStart \
		--redefine-sym $(USER_CALC_CODE_BIN_SYMBOL_BASE)_end=gKiCalcCodeBytesEnd \
		$@

$(TARGET_USER_CALC_CONST_OBJ): $(TARGET_USER_CALC_CONST_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_CALC_CONST_BIN_SYMBOL_BASE)_start=gKiCalcConstBytesStart \
		--redefine-sym $(USER_CALC_CONST_BIN_SYMBOL_BASE)_end=gKiCalcConstBytesEnd \
		$@

$(TARGET_USER_TICK1S_CODE_OBJ): $(TARGET_USER_TICK1S_CODE_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_TICK1S_CODE_BIN_SYMBOL_BASE)_start=gKiTick1sCodeBytesStart \
		--redefine-sym $(USER_TICK1S_CODE_BIN_SYMBOL_BASE)_end=gKiTick1sCodeBytesEnd \
		$@

$(TARGET_USER_TICK1S_CONST_OBJ): $(TARGET_USER_TICK1S_CONST_BIN)
	@mkdir -p $(dir $@)
	@echo "BINOBJ $@"
	@$(LD) -r -b binary -o $@ $<
	@$(OBJCOPY) --rename-section .data=.rodata,alloc,load,readonly,data,contents \
		--redefine-sym $(USER_TICK1S_CONST_BIN_SYMBOL_BASE)_start=gKiTick1sConstBytesStart \
		--redefine-sym $(USER_TICK1S_CONST_BIN_SYMBOL_BASE)_end=gKiTick1sConstBytesEnd \
		$@

$(USR_OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	@echo "USER CC $<"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USR_OBJDIR)/%.o: src/%.S
	@mkdir -p $(dir $@)
	@echo "USER AS $<"
	@$(CC) $(USER_CFLAGS) -o $@ $<

# ------------------------------------------------------------------------------
# Runtime artifacts for QEMU (vvfat): always sync the current build flavor.
# Without explicit profile variables, run-like targets default to the
# interactive demo_shell entry profile in a dedicated build flavor.
# ------------------------------------------------------------------------------
ESP_BOOT_EFI   := esp/EFI/BOOT/BOOTX64.efi
ESP_KERNEL_BIN := esp/kernel.bin

copy: $(TARGET_EFI) $(TARGET_KERNEL)
	@mkdir -p $(dir $(ESP_BOOT_EFI)) $(dir $(ESP_KERNEL_BIN))
	@cp $(TARGET_EFI) $(ESP_BOOT_EFI)
	@cp $(TARGET_KERNEL) $(ESP_KERNEL_BIN)
	@echo "Copied current build flavor to esp/ directory."

run: copy
	@if [ -z "$(OVMF_CODE)" ] || [ ! -r "$(OVMF_CODE)" ]; then \
		echo "ERROR: OVMF firmware not found/readable."; \
		echo "Set it explicitly, e.g. make run OVMF_CODE=/usr/share/edk2/x64/OVMF.4m.fd"; \
		exit 1; \
	fi
	@echo "Starting VM with EFI (mode=$(QEMU_ACCEL_MODE), cpu=$(QEMU_CPU_FLAGS))..."
	@$(SUDO_RUN) qemu-system-x86_64 \
		-m 512M \
		-bios "$(OVMF_CODE)" \
		-net none \
		-display $(QEMU_DISPLAY) \
		-cpu $(QEMU_CPU_FLAGS) $(QEMU_ACCEL_ARGS) \
		$(QEMU_MONITOR_ARG) \
		-drive file=fat:rw:esp,index=0,format=vvfat \
		-serial stdio

test:
ifeq ($(TEST_MODULE),list)
	@echo "Available test modules:"
	@echo "  schedule - scheduler demo suite (previous make run / make test all behavior)"
	@echo "  guard_wait - panic demo: wait while holding a critical section"
	@echo "  owned_exit - panic demo: exit while owning a mutex"
	@echo "  irql_wait  - panic demo: zero-timeout wait while holding DISPATCH_LEVEL guard"
	@echo "  irql_sleep - panic demo: sleep while holding DISPATCH_LEVEL guard"
	@echo "  irql_yield - panic demo: yield while holding DISPATCH_LEVEL guard"
	@echo "  irql_exit  - panic demo: thread exit while holding DISPATCH_LEVEL guard"
	@echo "  pf_imported - page-fault demo: NX execute fault in imported kernel-data region"
	@echo "  pf_guard    - page-fault demo: access thread stack guard page"
	@echo "  pf_fixmap   - page-fault demo: NX execute fault in active fixmap slot"
	@echo "  pf_heap     - page-fault demo: NX execute fault in heap-backed KVA page"
	@echo "  kthread_pool_race - regression suite for KTHREAD pool synchronization"
	@echo "  user_hello  - compiled minimal userspace hello regression profile"
	@echo "  user_caps   - staged bootstrap stdout capability pilot regression"
	@echo "  user_dual   - launch compiled user_hello and user_counter together"
	@echo "  demo_shell  - P2 demo-shell control-plane regression profile"
	@echo "Recommended explicit workflow:"
	@echo "  make clean"
	@echo "  # schedule"
	@echo "  bear -- make all BUILD_FLAVOR=test-schedule HO_DEMO_TEST_NAME=schedule HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_SCHEDULE"
	@echo "  BUILD_FLAVOR=test-schedule HO_DEMO_TEST_NAME=schedule HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_SCHEDULE \\"
	@echo "      bash scripts/qemu_capture.sh 30 /tmp/himuos-schedule.log"
	@echo "  # kthread_pool_race"
	@echo "  bear -- make all BUILD_FLAVOR=test-kthread_pool_race HO_DEMO_TEST_NAME=kthread_pool_race HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_KTHREAD_POOL_RACE"
	@echo "  BUILD_FLAVOR=test-kthread_pool_race HO_DEMO_TEST_NAME=kthread_pool_race HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_KTHREAD_POOL_RACE \\" 
	@echo "      bash scripts/qemu_capture.sh 30 /tmp/himuos-kthread-pool-race.log"
	@echo "  # user_dual (timing-sensitive: collect both host and tcg evidence)"
	@echo "  make clean"
	@echo "  bear -- make all BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL"
	@echo "  BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL \\"
	@echo "      QEMU_CAPTURE_MODE=host bash scripts/qemu_capture.sh 30 /tmp/himuos-user-dual-host.log"
	@echo "  BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL \\"
	@echo "      QEMU_CAPTURE_MODE=tcg bash scripts/qemu_capture.sh 30 /tmp/himuos-user-dual-tcg.log"
	@echo "  # user_input (requires scripted PS/2 key injection on both host and tcg)"
	@echo "  make clean"
	@echo "  bear -- make all BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT"
	@echo "  BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT \\"
	@echo "      QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/user_input.plan \\"
	@echo "      bash scripts/qemu_capture.sh 20 /tmp/himuos-user-input-host.log"
	@echo "  BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT \\"
	@echo "      QEMU_CAPTURE_MODE=tcg QEMU_SENDKEY_PLAN=scripts/input_plans/user_input.plan \\"
	@echo "      bash scripts/qemu_capture.sh 20 /tmp/himuos-user-input-tcg.log"
	@echo "  # demo_shell (requires scripted PS/2 key injection on both host and tcg)"
	@echo "  make clean"
	@echo "  bear -- make all BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL"
	@echo "  BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL \\"
	@echo "      QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/demo_shell.plan \\"
	@echo "      bash scripts/qemu_capture.sh 25 /tmp/himuos-demo-shell-host.log"
	@echo "  BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL \\"
	@echo "      QEMU_CAPTURE_MODE=tcg QEMU_SENDKEY_PLAN=scripts/input_plans/demo_shell.plan \\"
	@echo "      bash scripts/qemu_capture.sh 25 /tmp/himuos-demo-shell-tcg.log"
	@echo "Usage:"
	@echo "  make test schedule   # run the scheduler demo suite"
	@echo "  make test irql_wait  # run a dispatch-guard misuse panic regression"
	@echo "  make test pf_heap    # run heap-backed page-fault observability demo"
	@echo "  make test kthread_pool_race # run the KTHREAD pool race regression suite"
	@echo "  make test user_hello # select the compiled minimal userspace hello profile"
	@echo "  make test user_caps  # select the staged bootstrap capability pilot profile"
	@echo "  make test user_dual  # select the dual compiled-userspace bring-up profile (use qemu_capture host+tcg)"
	@echo "  make test user_input # select the bounded demo-shell input profile (use qemu_capture host+tcg with sendkeys)"
	@echo "  make test demo_shell # select the P2 demo-shell control-plane profile (use qemu_capture host+tcg with sendkeys)"
	@echo "  make test            # list available test modules"
else
	@echo "Starting test module: $(TEST_MODULE)"
	@$(MAKE) run BUILD_FLAVOR=$(TEST_BUILD_FLAVOR) HO_DEMO_TEST_NAME=$(TEST_MODULE) HO_DEMO_TEST_DEFINE=$(TEST_DEFINE_$(TEST_MODULE))
endif

schedule user_hello user_caps user_dual user_input demo_shell list:
	@:
		
debug: copy
	@if [ -z "$(OVMF_CODE)" ] || [ ! -r "$(OVMF_CODE)" ]; then \
		echo "ERROR: OVMF firmware not found/readable."; \
		echo "Set it explicitly, e.g. make debug OVMF_CODE=/usr/share/edk2/x64/OVMF.4m.fd"; \
		exit 1; \
	fi
	@echo "Starting VM with EFI and GDB server..."
	@qemu-system-x86_64 \
		-m 512M \
		-bios "$(OVMF_CODE)" \
		-net none \
		-display $(QEMU_DISPLAY) \
		-cpu $(QEMU_CPU_FLAGS) \
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
	@if [ -z "$(OVMF_CODE)" ] || [ ! -r "$(OVMF_CODE)" ]; then \
		echo "ERROR: OVMF firmware not found/readable."; \
		echo "Set it explicitly, e.g. make run_iso OVMF_CODE=/usr/share/edk2/x64/OVMF.4m.fd"; \
		exit 1; \
	fi
	@echo "Starting ISO VM with EFI (mode=$(QEMU_ACCEL_MODE), cpu=$(QEMU_CPU_FLAGS))..."
	qemu-system-x86_64 \
    -m 512M \
    -bios "$(OVMF_CODE)" \
    -net none \
    -display $(QEMU_DISPLAY) \
    -cpu $(QEMU_CPU_FLAGS) $(QEMU_ACCEL_ARGS) \
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
USER_CFLAGS += -MMD -MP

# Include auto-generated dependency files (if they exist)
-include $(OBJS_EFI_C:.o=.d)
-include $(OBJS_KERNEL_C:.o=.d)
-include $(OBJS_USER_HELLO_C:.o=.d)
-include $(OBJS_USER_COUNTER_C:.o=.d)
-include $(OBJS_USER_HSH_C:.o=.d)
-include $(OBJS_USER_CALC_C:.o=.d)
-include $(OBJS_USER_TICK1S_C:.o=.d)
-include $(OBJS_USER_HELLO_S:.o=.d)
QEMU_MONITOR_ARG := $(if $(strip $(QEMU_MONITOR_SOCKET)),-monitor unix:$(QEMU_MONITOR_SOCKET)$(comma)server$(comma)nowait)
