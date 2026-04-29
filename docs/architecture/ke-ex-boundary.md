# Ke / Ex Boundary Baseline

This document records the current boundary between the Ke mechanism layer and
the Ex policy layer before the New Era refactor. Stage 0 preserves behavior; it
does not move code.

## Intended Rule

Ke owns mechanisms. Ex owns resources, policy, and user-visible contracts. Demo
code proves contracts; it should not define them.

In practical terms:

- Ke may switch address spaces, schedule threads, wait on dispatcher objects,
  copy validated user bytes, route traps, and expose bounded mechanism snapshots.
- Ex should own process and thread identity, object and handle lifetime, syscall
  numbers and return policy, program lookup, foreground process policy, and
  user-facing sysinfo views.
- Demo code should launch scenarios and validate anchors, not host permanent
  process-control APIs.

## Current Ke Responsibilities That Are Healthy

- `src/kernel/ke/mm` owns imported address-space metadata, page-table
  operations, KVA, heap foundation, allocator, and pool support.
- `src/kernel/ke/thread` owns `KTHREAD`, scheduling, wait, sleep, join, detach,
  and reaper mechanics.
- `src/kernel/ke/time` owns time-source and clock-event mechanisms.
- `src/kernel/ke/console` and `src/kernel/ke/input` own device/sink mechanics.
- `src/kernel/ke/sysinfo` owns structured Ke mechanism snapshots.

These are the mechanisms later Ex code should consume rather than duplicate.

## Current Boundary Debts

- `src/kernel/ke/user_bootstrap_syscall.c` dispatches user-visible policy
  syscalls: `SYS_READLINE`, `SYS_SPAWN_PROGRAM`, `SYS_WAIT_PID`,
  `SYS_SLEEP_MS`, and `SYS_KILL_PID`.
- The same Ke file includes `kernel/demo_shell.h` and checks
  `KeDemoShellShouldTerminateCurrentThread()` after each syscall.
- `src/kernel/ke/input/input.c` depends on `ex_bootstrap_abi.h` for the
  bootstrap line capacity.
- `src/include/kernel/ke/bootstrap_callbacks.h` names the ownership,
  address-space-root, finalize, timer-observe, and user-exception callbacks as
  bootstrap callbacks even though they now describe the user-runtime bridge.
- `src/kernel/demo/demo_shell_runtime.c` implements process-control policy
  while exporting `KeDemoShell*` functions.
- `ExBootstrapBorrowKernelThread()` exposes a `KTHREAD *` to demo code so it can
  join bootstrap user threads directly.
- `ex_bootstrap_abi.h` mixes layout constants, syscall numbers, program name
  limits, sysinfo structs, and log anchors.

These debts are acceptable in Stage 0 because they are part of the current
working MVP. Later stages should remove them profile by profile.

## Target Direction After Stage 0

The first refactor stage should preserve the existing profiles while moving
policy behind Ex-owned APIs:

- Keep the `int 0x80` trap registration in Ke while making the trap call an
  Ex syscall dispatcher.
- Keep raw bootstrap syscalls only for low-level regression profiles.
- Move `readline`, `spawn`, `wait`, `sleep`, `kill`, `write`, `close`,
  `wait_one`, `query_sysinfo`, and `exit` policy into Ex.
- Extract object, handle, process, thread, syscall, sysinfo, program, and image
  modules from the current bootstrap files.
- Replace demo-shell child tables and runtime aliases with Ex process/thread
  tables and Ex waitable process or thread handles.
- Rename bootstrap callback concepts only after equivalent generic owner/root
  contracts exist.

## Non-Goals For Stage 0

Stage 0 must not:

- change syscall numbers, return values, or register ABI
- change user program layout or embedded artifact rules
- split `ex_bootstrap.c` or `ex_bootstrap_adapter.c`
- remove raw bootstrap syscalls
- replace demo shell control-plane behavior
- alter QEMU profile input plans or expected anchors unless the existing profile
  is already failing and the failure is documented

The Stage 0 job is to make the current architecture and regression contracts
explicit before behavior moves.
