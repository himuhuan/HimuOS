# Ke / Ex Boundary Notes

This document records the Phase A cleanup baseline for the Ke mechanism layer
and the Ex user-runtime layer.

## Working Rule

Ke owns mechanisms. Ex owns user-visible runtime policy and resource identity.
Demo code proves contracts; it must not define them.

In practical terms:

- Ke may switch address spaces, schedule threads, wait on dispatcher objects,
  copy validated user bytes, route traps, and expose bounded mechanism
  snapshots.
- Ex should own process/thread identity, object and handle lifetime, syscall
  policy, program lookup, foreground ownership, and user-facing sysinfo views.
- Demo code should launch scenarios and validate anchors, not host permanent
  runtime internals.

## Ke Responsibilities That Are Already Stable

- `src/kernel/ke/mm` owns imported address-space metadata, page-table
  operations, KVA, heap foundation, allocator, and pool support.
- `src/kernel/ke/thread` owns `KTHREAD`, scheduling, wait, sleep, join, detach,
  and reaper mechanics.
- `src/kernel/ke/time` owns time-source and clock-event mechanisms.
- `src/kernel/ke/console` and `src/kernel/ke/input` own device/sink mechanics.
- `src/kernel/ke/sysinfo` owns structured Ke mechanism snapshots.

These are the mechanisms Ex should consume rather than duplicate.

## Ex Responsibilities That Are Already Live

- `src/kernel/ex/object.c`, `src/kernel/ex/handle.c`,
  `src/kernel/ex/process.c`, and `src/kernel/ex/thread.c` host the current
  object, handle, process, and thread scaffolding.
- `src/kernel/ex/syscall.c` dispatches the formal `SYS_*` user-runtime ABI.
- `src/kernel/ex/process_control.c` owns the current spawn/wait/kill/
  foreground policy surface used by `demo_shell` and `user_fault`.
- `src/kernel/ex/program.c` maps embedded program names and IDs to user images.
- `src/kernel/ex/ex_bootstrap.c` is the init facade, and
  `src/kernel/ex/ex_bootstrap_adapter.c` is only a compatibility translation
  unit.

## Named Boundary Debts

- `src/include/kernel/ke/bootstrap_callbacks.h` still names the ownership,
  address-space-root, finalize, timer-observe, and user-fault hooks as
  bootstrap callbacks even though they now describe the user-runtime bridge;
  `src/arch/amd64/idt.c` still dispatches through those callback names.
- `src/include/kernel/ke/user_bootstrap.h` and
  `src/kernel/ke/user_bootstrap_syscall.c` still own low-level trap, user-copy,
  and user-entry helpers with bootstrap names.
- `src/kernel/ex/runtime_alias.c` still maps runtime process/thread identity
  through temporary bootstrap alias slots keyed by `KTHREAD *`.
- `src/kernel/ex/process_control.c` still keeps an ad hoc child table with a
  borrowed `KTHREAD *` as the wait/kill anchor.
- `src/kernel/demo/user_input.c` still calls `ExBootstrapBorrowKernelThread()`
  directly, and both `user_input` and `user_dual` still build userspace through
  `ExBootstrap*` launch helpers instead of permanent Ex runtime APIs.
- `src/include/kernel/ex/ex_bootstrap_abi.h` still mixes layout constants,
  syscall numbers, spawn flags, sysinfo structs, and regression anchors;
  direct consumers still include `src/arch/amd64/idt.c`,
  `src/kernel/ex/ex_bootstrap_internal.h`, `src/include/kernel/ex/program.h`,
  `src/include/kernel/ke/input.h`, `src/include/kernel/ke/user_bootstrap.h`,
  `src/kernel/demo/demo_internal.h`, `src/user/libsys.h`, and
  `src/user/crt0.S`.

The owner phase and deletion condition for each debt are tracked in
`docs/architecture/bootstrap-debt-index.md`.

## Current Profile Reality

- `demo_shell` and `user_fault` already validate the Ex-owned control plane.
- `user_input` and `user_dual` are still official contracts, but they remain on
  transitional launch plumbing.
- `user_hello` and `user_caps` are legacy bring-up sentinels, not the default
  runtime story.

## Near-Term Non-Goals

Near-term cleanup must not:

- change syscall numbers, return values, or register ABI
- change user program layout or embedded artifact rules
- split `ex_bootstrap.c` or `ex_bootstrap_adapter.c`
- remove raw bootstrap syscalls before their sentinel value is retired
- replace demo shell lifecycle behavior with full TTY-lite or POSIX job control
- alter QEMU profile input plans or expected anchors unless the existing
  profile is already failing and the failure is documented

The near-term job is to keep the current boundary explicit while later phases
move behavior behind smaller, permanent Ex-owned contracts.
