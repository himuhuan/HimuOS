# HimuOS Architecture Overview

This document records the current cleanup baseline: what is true in-tree now
after the runtime Bootstrap compatibility names were retired.

## Boot And Handoff

`Bootstrap` should mean early machine boot only. In the current tree that means
the UEFI loader under `src/boot/v2` plus the x86_64 kernel handoff contract.
The loader builds the boot capsule and mapping manifest, then transfers control
with framebuffer, memory map, kernel placement, stacks, page-table metadata,
ACPI RSDP, and CPU-local data already prepared.

The loader does not own userspace policy or program loading. Once the kernel is
running, runtime program lookup and launch belong to Ex.

## Current Ke / Ex Split

Ke owns mechanisms:

- interrupt, trap, and low-level user entry/return mechanics
- PMM, imported address-space metadata, page-table operations, KVA, heap, and
  pool foundations
- kernel threads, scheduler state, waits, sleeps, and dispatcher objects
- time-source and clock-event abstraction
- console and input device mechanisms
- bounded mechanism snapshots through `KeQuerySystemInformation()`

Ex owns the current user-runtime policy surface:

- embedded user-image lookup in `src/kernel/ex/program.c`
- object, handle, process, and thread scaffolding in
  `src/kernel/ex/object.c`, `src/kernel/ex/handle.c`,
  `src/kernel/ex/process.c`, and `src/kernel/ex/thread.c`
- runtime process/thread tables in `src/kernel/ex/runtime_table.c`, with
  user-facing sysinfo rendering in `src/kernel/ex/sysinfo.c`
- syscall dispatch in `src/kernel/ex/syscall.c`
- the current spawn/wait/kill/foreground control plane in
  `src/kernel/ex/process_control.c`
- `src/kernel/ex/runtime.c` is the Ex runtime init facade
- user-fault handoff and Ke bridge glue in `src/kernel/ex/user_runtime_bridge.c`

The split is now reflected in active runtime names: Ke owns low-level
user-mode mechanisms, while Ex owns runtime policy and launch surfaces.
Historical cleanup context remains in `docs/architecture/bootstrap-debt-index.md`.

## Current User-Runtime Shape

Demo profiles live in `src/kernel/demo`. User programs live in `src/user` and
are still embedded into the kernel build through make rules and bridge files.

Current runtime reality:

- `demo_shell` is the visible vertical slice: boot, launch `hsh`, then drive
  `sysinfo`, `memmap`, `ps`, foreground `calc`, background `tick1s`, `kill`,
  and `exit`.
- Ex process/thread identity now flows through the runtime process and thread
  tables. The old runtime alias registry and process-control child table are
  retired.
- Ex process/thread objects now own completion state. Process wait/kill and
  generic wait handles no longer depend on pilot wait objects or borrowed
  `KTHREAD` joins.
- `demo_shell`, `user_fault`, `user_input`, and `user_dual` exercise the
  Ex-owned `ExSpawnProgram()` / `ExWaitProcess()` / `ExKillProcess()` control
  plane. `user_input` also uses `ExSetForegroundProcess()` for foreground
  handoff.
- `user_hello` is a minimal formal-ABI smoke profile, while `user_caps` is the
  only remaining legacy raw/P1 bring-up sentinel.
- `hsh`, `calc`, `tick1s`, `fault_de`, `fault_pf`, `user_counter`, and
  `user_hello` include the normal `src/user/libsys.h` wrapper surface over the
  formal `EX_USER_SYS_*` services and enter without phase-gate waits. The
  kernel-embedded `user_caps` payload remains an explicit raw syscall /
  phase-gate sentinel until that path is retired.
- Structured Ex sysinfo classes are the stable userspace contract. `hsh ps`
  queries `EX_SYSINFO_CLASS_PROCESS_LIST` and formats the process view in user
  space; text sysinfo classes remain convenience renderers for `sysinfo`,
  `memmap`, and compatibility callers.

## Build And Regression

The main build file is `makefile`. The default interactive `run`, `debug`,
`iso`, and `run_iso` paths select the `demo_shell` profile unless explicit
profile variables are supplied.

Regression is profile-driven through QEMU serial logs. Use
`scripts/qemu_capture.sh` for captures, and use scripted input plans for the
interactive profiles:

- `scripts/input_plans/user_input.plan`
- `scripts/input_plans/demo_shell.plan`
- `scripts/input_plans/user_fault.plan`

The timing-sensitive safety net is `demo_shell`, `user_input`, `user_dual`, and
`user_fault`; each still requires both `QEMU_CAPTURE_MODE=host` and
`QEMU_CAPTURE_MODE=tcg` evidence before behavior-changing cleanup is considered
safe. The canonical contract/sentinel index is `docs/regression-profiles.md`.

## Documentation Baseline

The cleanup baseline is healthy when:

- this architecture map matches the checked-in implementation
- `new_era_next.md` remains the cleanup roadmap source
- `docs/architecture/ke-ex-boundary.md` records the current Ke/Ex split
- `docs/architecture/bootstrap-debt-index.md` records retired Bootstrap-era
  runtime concepts and confirms that remaining Bootstrap names are true
  early-boot state or historical notes
- `docs/apis/ExUserSysinfoABI.md` separates stable structured sysinfo classes
  from text presentation helpers
- `docs/regression-profiles.md` records the contract/sentinel split and capture
  expectations
- `make all` and `make test list` are known-good, or any environment blocker is
  recorded
- host and TCG evidence for timing-sensitive profiles is captured when touched,
  or the blocker is recorded
