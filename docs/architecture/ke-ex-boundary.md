# Ke / Ex Boundary Notes

This document records the current cleanup baseline for the Ke mechanism layer
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
- `src/kernel/ex/runtime_table.c` owns the live Ex process/thread lookup model
  used by sysinfo, wait/kill lookup, fault handling, and foreground metadata.
- `src/kernel/ex/process_control.c` owns the current spawn/wait/kill/
  foreground policy surface used by `demo_shell` and `user_fault`.
- `src/kernel/ex/program.c` maps embedded program names and IDs to user images.
- `src/kernel/ex/ex_bootstrap.c` is the init facade, and
  `src/kernel/ex/ex_bootstrap_adapter.c` is only a compatibility translation
  unit.

## Named Boundary Debts

- `src/include/kernel/ke/user_bootstrap.h` and
  `src/kernel/ke/user_bootstrap_syscall.c` still own low-level trap, user-copy,
  and user-entry helpers with bootstrap names.

## Phase F Demo Runtime APIs

The contract demo profiles now launch scenarios through permanent Ex process
control:

- `user_dual` uses `ExSpawnProgram()` and `ExWaitProcess()` for its concurrent
  `user_hello` / `user_counter` evidence.
- `user_input` uses `ExSpawnProgram()`, `ExSetForegroundProcess()`, and
  `ExWaitProcess()` for foreground handoff and cleanup evidence.

Those profiles no longer call `ExBootstrap*` launch helpers, borrow backing
`KTHREAD` pointers, or join user threads directly.

## Phase E Waitability

Pilot wait objects are retired. Ex process and thread objects own embedded
completion events, and `SYS_WAIT_ONE` resolves wait-right handles directly to
process or thread objects.

`ExWaitProcess()` and `ExKillProcess()` now wait on the retained child process
completion state. They no longer borrow the process main backing `KTHREAD`, and
normal Ex-spawned user threads are detached so the idle reaper finalizes
user-runtime resources and signals process completion.

## Phase D Runtime Tables

The runtime alias registry and process-control child table are retired.
`src/kernel/ex/runtime_table.c` now publishes bounded Ex process and thread
tables. Process objects carry parent PID, main TID, lifecycle state, exit
status, termination reason, kill request, foreground restore metadata, and
completion state.

## Phase H Structured Sysinfo

`src/include/kernel/ex/user_sysinfo_abi.h` is the stable user-facing sysinfo
contract. `EX_SYSINFO_CLASS_OVERVIEW`, `EX_SYSINFO_CLASS_PROCESS_LIST`, and
`EX_SYSINFO_CLASS_THREAD_LIST` return structured data. Text classes are
presentation helpers over those structures or, for `MEMMAP_TEXT`, over Ke
VMM/KVA snapshots and the current user layout.

`hsh ps` uses `EX_SYSINFO_CLASS_PROCESS_LIST` and formats the view in user
space. Process and thread identities come from Ex runtime tables; Ke contributes
mechanism snapshots through `KeQuerySystemInformation()`.

## Phase C User-Runtime Hooks

Ke now exposes the permanent user-runtime hook contract through
`src/include/kernel/ke/user_runtime_hooks.h` and
`src/kernel/ke/user_runtime_hooks.c`. Scheduler dispatch, timer observation,
and IDT user-fault handoff call user-runtime owner/root/finalize/timer/fault
hooks instead of bootstrap callback names.

Ex implements that hook contract in `src/kernel/ex/user_runtime_bridge.c`.
The bridge still calls transitional `ExBootstrapAdapter*()` helpers because the
process/thread identity and launch surfaces are later-phase debts, but those
helper names are no longer part of the Ke callback boundary.

## Phase B ABI Split

The old `src/include/kernel/ex/ex_bootstrap_abi.h` umbrella is gone. Stable
user-runtime ABI now lives in focused contracts:

- `src/include/kernel/ex/user_syscall_abi.h`
- `src/include/kernel/ex/user_sysinfo_abi.h`
- `src/include/kernel/ex/user_image_abi.h`
- `src/include/kernel/ex/user_capability_abi.h`

Bring-up-only raw syscall, P1 mailbox, and regression-log anchors are separate:

- `src/include/kernel/ex/user_bringup_sentinel_abi.h`
- `src/include/kernel/ex/user_regression_anchors.h`

`src/user/libsys.h` is the normal userspace wrapper surface. Raw helpers and
phase-gate waiting are isolated in `src/user/libsys_bringup.h` for the
`user_hello` sentinel and in the kernel-embedded `user_caps` payload.

The owner phase and deletion condition for each debt are tracked in
`docs/architecture/bootstrap-debt-index.md`.

## Current Profile Reality

- `demo_shell`, `user_fault`, `user_input`, and `user_dual` validate the
  Ex-owned control plane.
- `user_hello` and `user_caps` are legacy raw/P1 bring-up sentinels, not the
  default runtime story.
- Normal userspace programs use `src/user/libsys.h` and do not wait on the P1
  mailbox.

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
