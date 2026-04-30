# HimuOS Architecture Overview

This document tracks the current New Era refactor state. It describes the
branch as it exists today, not the final target.

## Boot And Handoff

The boot path is UEFI-first and lives under `src/boot/v2`. The loader builds a
boot capsule and mapping manifest, then hands control to the x86_64 kernel with
framebuffer, memory map, kernel placement, stacks, page-table metadata, ACPI
RSDP, and CPU-local data already described.

The current boot contract is good enough for the teaching-kernel target. Stage 0
does not expand the loader or turn it into a runtime program loader. Runtime
program loading remains an Ex responsibility for later stages.

## Ke Mechanism Layer

The Ke layer owns kernel mechanisms:

- interrupt and trap setup
- PMM, imported address-space metadata, page-table operations, KVA, heap, and
  pool foundations
- kernel threads, scheduler state, waits, sleeps, and dispatcher objects
- time-source and clock-event abstraction
- console and input mechanisms
- bounded system information snapshots through `KeQuerySystemInformation()`
- the current bootstrap user mapping and trap mechanics

The strongest current subsystem is memory management:
Boot memory map -> PMM -> imported kernel address space -> PT HAL -> KVA ->
heap foundation -> allocator and pool consumers.

Ke still owns the low-level bootstrap syscall trap and user-copy mechanics in
`src/kernel/ke/user_bootstrap_syscall.c`, but user-visible syscall policy now
enters the Ex dispatcher in `src/kernel/ex/syscall.c`.

## Ex Bootstrap Facade

The Ex layer currently exists, but it is still bootstrap-shaped. The public
headers are mostly:

- `src/include/kernel/ex/ex_bootstrap.h`
- `src/include/kernel/ex/ex_bootstrap_adapter.h`
- `src/include/kernel/ex/ex_bootstrap_abi.h`
- `src/include/kernel/ex/ex_process.h`
- `src/include/kernel/ex/ex_thread.h`

The implementation is concentrated in two large files:

- `src/kernel/ex/ex_bootstrap.c`
- `src/kernel/ex/ex_bootstrap_adapter.c`

Those files already contain the seeds of the intended Executive Lite layer:
object headers, process and thread wrappers, private generation-checked handles,
rights bits, stdout and waitable pilot objects, process-private address spaces,
sysinfo text rendering, capability syscalls, user-fault handling, and callback
bridges into Ke.

Later stages should continue extracting object, handle, process, thread,
syscall, program, image, and sysinfo modules without changing profile behavior
first.

## Demo And User Programs

Demo profiles live in `src/kernel/demo`. User programs live in `src/user`.

Current official user-visible programs are compiled C userspace artifacts:

- `hsh`
- `calc`
- `tick1s`
- `user_hello`
- `user_counter`

They are still embedded into the kernel build through make rules and artifact
bridge files. `demo_shell` is the visible MVP vertical slice: the kernel boots,
launches user-mode `hsh`, and the shell drives `sysinfo`, `memmap`, `ps`,
foreground `calc`, background `tick1s`, `kill`, and `exit`.

The demo shell launcher under `src/kernel/demo` is now a thin profile wrapper.
The temporary shell lifecycle control plane lives in
`src/kernel/ex/process_control.c`, where `spawn`, `wait`, `kill`, cooperative
kill observation, and foreground restoration are Ex-owned.

## Build And Regression

The main build file is `makefile`. The default interactive `run`, `debug`,
`iso`, and `run_iso` paths select the `demo_shell` profile unless explicit
profile variables are supplied.

Regression is profile-driven through QEMU serial logs. Use
`scripts/qemu_capture.sh` for captures, and use scripted input plans for
interactive profiles:

- `scripts/input_plans/user_input.plan`
- `scripts/input_plans/demo_shell.plan`
- `scripts/input_plans/user_fault.plan`

Timing-sensitive user/process profiles require both `QEMU_CAPTURE_MODE=host`
and `QEMU_CAPTURE_MODE=tcg` evidence before later refactors are considered
safe. The canonical profile contract index is `docs/regression-profiles.md`.

## Regression Criteria

Refactor checkpoints are healthy when:

- this architecture map exists and matches the checked-in baseline
- `new_era_plan.md` is tracked as the New Era roadmap source
- `docs/architecture/ke-ex-boundary.md` records the current boundary debts and
  future direction
- `docs/regression-profiles.md` records profile commands, anchors, and evidence
  expectations
- `make all` and `make test list` are known-good, or any environment blocker is
  recorded
- host and TCG evidence for timing-sensitive user/process profiles is captured,
  or the blocker is recorded
