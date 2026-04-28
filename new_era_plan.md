# HimuOS New Era Plan

> Date: 2026-04-28  
> Branch observed: `beta/mvp-presentation`  
> Goal: turn a deadline-driven demo branch into a maintainable teaching operating system.

## 0. Executive Summary

HimuOS is no longer just a booting toy. The current branch already contains a modern UEFI loader, a boot capsule handoff,
an x86_64 kernel with IDT/GDT/TSS setup, a PMM/KVA/allocator stack, a priority-aware tickless scheduler, synchronization
objects, clock/time-source abstraction, console/input sinks, minimal Ring 3 entry, system calls, and several user programs.
As an MVP, the `demo_shell` vertical slice succeeded.

The main architectural problem is that the system's public story says "Ke is mechanism and Ex is policy", but the code
does not yet consistently enforce that boundary. `Ke` is comparatively strong and increasingly well documented. `Ex` is
still a bootstrap facade: process, thread, object, handle, syscall, sysinfo rendering, exception policy, and demo-shell
control-plane logic are all fused around the initial user-bootstrap path.

The new era should not try to become Linux, NT, or a production kernel. It should become a coherent teaching kernel:
small enough to understand, but complete enough to demonstrate the classical operating-system tasks:

- boot and hardware handoff
- physical and virtual memory management
- kernel threads, scheduling, synchronization, sleep/wakeup
- process/address-space isolation
- syscall ABI and handle/capability-based kernel services
- user program loading and execution
- terminal-like input/output
- fault handling and process teardown
- observability through `sysinfo`/`ps`/`memmap`-style views

The recommended direction is: keep the good mechanisms, retire the deadline scaffolding, and promote `Ex` from
"bootstrap wrapper" to a small but real executive layer.

## 1. Current Architecture Map

### 1.1 Boot And Handoff

Relevant code:

- `src/boot/v2`
- `src/boot/v2/blmm`
- `src/include/boot/boot_capsule.h`
- `src/include/boot/boot_mapping_manifest.h`

Current strengths:

- UEFI-first boot path.
- Boot capsule packages framebuffer, memory map, kernel placement, stacks, page-table info, ACPI RSDP, and CPU-local
  metadata.
- Boot Mapping Manifest gives the kernel a describable imported address space instead of forcing later code to infer
  everything from raw boot structures.
- NULL-page guard and NX-awareness are good teaching details.

Current issues:

- Boot/Ke documentation is stronger than the rest of the project, but some docs still carry stale absolute links from
  older workspaces.
- Loader policy, initial mapping policy, and handoff validation are good enough for the teaching target. They should not
  be the next major rewrite target unless a bug is found.

Recommendation:

- Keep boot architecture mostly intact.
- Add one high-level "Himu Boot Contract" document that freezes what the loader promises to Ke.
- Avoid expanding the bootloader into a runtime loader. Runtime program loading belongs above Ke, in Ex.

### 1.2 Ke Layer

Relevant code:

- `src/kernel/ke/mm`
- `src/kernel/ke/pmm`
- `src/kernel/ke/thread`
- `src/kernel/ke/time`
- `src/kernel/ke/console`
- `src/kernel/ke/input`
- `src/kernel/ke/sysinfo`

Current strengths:

- The memory stack is the cleanest part of the system:
  `Boot memory map -> PMM -> imported kernel address space -> PT HAL -> KVA -> heap foundation -> allocator/pool`.
- KVA has real ownership semantics, guard pages, generation-checked handles, and observability.
- Scheduler has a real state machine: `NEW`, `READY`, `RUNNING`, `BLOCKED`, `TERMINATED`.
- Join/detach/reaper semantics have already been tightened.
- Time-source/clock-event and console use a device/sink style that is easy to teach.
- `KeQuerySystemInformation()` is a strong observability anchor.

Current issues:

- Ke currently knows too much about the bootstrap user path through `bootstrap_callbacks` and `user_bootstrap`.
- `src/kernel/ke/user_bootstrap_syscall.c` still owns policy syscalls such as `readline`, `spawn_builtin`, `wait_pid`,
  `sleep_ms`, and `kill_pid`.
- `src/kernel/ke/input/input.c` includes `ex_bootstrap_abi.h` just to inherit line capacity. That is a boundary smell:
  a hardware/input mechanism should not depend on Ex bootstrap ABI constants.
- The scheduler asks a "bootstrap root callback" to resolve a dispatch root. That mechanism should become a generic
  address-space binding hook, not a bootstrap-specific concept.

Recommendation:

- Keep Ke as the mechanism layer.
- Shrink Ke's user-mode knowledge to traps, copyin/copyout mechanisms, address-space switching, dispatcher objects, and
  thread scheduling.
- Move user-visible policy and demo-shell control semantics into Ex.

### 1.3 Ex Layer

Relevant code:

- `src/kernel/ex/ex_bootstrap.c` around 1569 lines
- `src/kernel/ex/ex_bootstrap_adapter.c` around 1504 lines
- `src/include/kernel/ex/ex_bootstrap_abi.h`
- `src/include/kernel/ex/ex_process.h`
- `src/include/kernel/ex/ex_thread.h`

Current strengths:

- There is already an embryonic object model:
  `EX_OBJECT_HEADER`, object types, reference count, process/thread/stdout/waitable objects.
- There is already a generation-checked private handle table with rights bits.
- Ex is the owner of process-private address-space creation for bootstrap userspace.
- Ex is the right place to make process/thread/user-contract policy real.

Current issues:

- The implementation is too monolithic. Object management, handle lookup, process lifetime, thread lifetime, runtime
  aliasing, capability seed patching, sysinfo capture, text rendering, exception policy, syscall dispatch, and callback
  bridge logic live in two large files.
- The layer is named around `Bootstrap`, so the architecture continues to think of user mode as a temporary bring-up
  trick even after it has become a core feature.
- `gExBootstrapRuntimeAliases` has a capacity of 4. That is fine for a demo but not a maintainable process model.
- `EX_PRIVATE_HANDLE_TABLE_CAPACITY` is 8. That is fine for a capability pilot but too small and too implicit for a
  teaching OS with multiple object kinds.
- `KiBootstrapWaitableCompanionThreadEntry()` creates a waitable handle by starting a companion thread only so something
  can be waited on. That is a pilot artifact, not a final object model.
- `ExBootstrapBorrowKernelThread()` leaks the underlying `KTHREAD` abstraction upward so demo code can coordinate. That
  should be replaced by Ex-owned wait/process APIs.
- `ex_bootstrap_abi.h` mixes fixed bootstrap layout constants, raw syscall numbers, formal syscall numbers, builtin
  program IDs, sysinfo structs, and log anchors.

Recommendation:

- Promote Ex into a real "Executive Lite" layer.
- Split object manager, handle table, process manager, thread manager, syscall dispatcher, program registry, and sysinfo
  facade into separate modules.
- Keep the current bootstrap path as a compatibility slice while migrating call sites.

### 1.4 Demo And User Programs

Relevant code:

- `src/kernel/demo`
- `src/user/hsh`
- `src/user/calc`
- `src/user/tick1s`
- `src/user/libsys.h`
- `src/user/user.ld`

Current strengths:

- Profiles act as regression contracts.
- `hsh`, `calc`, and `tick1s` make the system visible and teachable.
- The user ABI is small enough for students to read.

Current issues:

- `src/kernel/demo/demo_shell_runtime.c` is effectively an Ex process-control plane but lives under `demo` and exports
  `KeDemoShell*` APIs.
- User program embedding is repeated manually in the makefile and bridged through one-file artifact adapters.
- Some user programs are simultaneously demos, tests, and official product behavior. That makes future changes risky.
- `hsh` is a shell-like demo, but it carries job-control semantics without a real process table or terminal model.

Recommendation:

- Keep demo profiles, but stop letting demo code own runtime policy.
- Make `demo_shell` a consumer of Ex process APIs, not the place where process APIs are invented.
- Move built-in program registration out of demo runtime and into an Ex program registry.

### 1.5 Build, Test, And Docs

Current strengths:

- The preferred QEMU capture workflow is explicit.
- Timing-sensitive profiles already distinguish host/KVM and TCG.
- Several fix reports document real race and memory bugs well.

Current issues:

- The makefile repeats per-program compile/link/embed rules.
- No generated manifest describes user programs.
- Regression evidence is mainly serial-log based, which is acceptable here, but the expected anchors are scattered.
- There is no architecture decision record process. This matters because the codebase is now entering a refactor-heavy
  phase where "why this shape" matters as much as "what changed".

Recommendation:

- Introduce `docs/adr/`.
- Introduce a simple `src/user/programs.mk` or generated program manifest.
- Keep log-anchor regression, but centralize expected anchors per profile.

## 2. Architectural Diagnosis

The beta branch succeeded because it built vertical slices. The next stage will fail if it keeps adding more vertical
slices without promoting their common concepts.

The key diagnosis is:

| Area | Current Shape | New-Era Shape |
| --- | --- | --- |
| User mode | bootstrap window plus embedded payloads | first-class process/image/address-space model |
| Ex | bootstrap facade | Executive Lite |
| Syscalls | split between Ke raw trap, Ke policy handlers, Ex adapter | one Ex syscall dispatcher behind a Ke trap mechanism |
| Process IDs | borrowed `KTHREAD::ThreadId` | Ex-owned PID/TID namespace |
| Handles | private capability pilot | Object Manager Lite handle table |
| Program loading | makefile embedding plus artifact bridge | Ex program registry now, ELF/initrd loader later |
| Shell control | demo runtime helper | Ex process APIs plus TTY-lite |
| Tests | demo profiles | profiles as explicit contracts |

The rule for the new era:

> Ke owns mechanisms. Ex owns resources, policies, and user-visible contracts. Demo code proves contracts; it does not
> define them.

## 3. Major Refactoring Plan

### P0. Split Ex Into Coherent Modules

Current problem:

- `ex_bootstrap.c` and `ex_bootstrap_adapter.c` are carrying too many responsibilities.

Target module layout:

- `src/kernel/ex/object.c`
  Object header, type metadata, reference management, destroy callbacks.
- `src/kernel/ex/handle.c`
  Handle table, rights checks, generation encoding, close/dup/query.
- `src/kernel/ex/process.c`
  `EX_PROCESS` lifecycle, PID allocation, process state, exit status, address-space ownership.
- `src/kernel/ex/thread.c`
  `EX_THREAD` lifecycle, TID allocation, binding to `KTHREAD`, join/wait/exit policy.
- `src/kernel/ex/image.c`
  User image description, fixed bootstrap image compatibility, future ELF segment loading.
- `src/kernel/ex/program.c`
  Builtin program registry: `hsh`, `calc`, `tick1s`, fault demos.
- `src/kernel/ex/syscall.c`
  Ex syscall table and dispatch.
- `src/kernel/ex/sysinfo.c`
  Ex-facing sysinfo classes and user-copy output.
- `src/kernel/ex/bootstrap_compat.c`
  Temporary adapter preserving old profiles during migration.

Public headers should become:

- `src/include/kernel/ex/object.h`
- `src/include/kernel/ex/handle.h`
- `src/include/kernel/ex/process.h`
- `src/include/kernel/ex/thread.h`
- `src/include/kernel/ex/syscall_abi.h`
- `src/include/kernel/ex/sysinfo_abi.h`
- `src/include/kernel/ex/program.h`

Migration strategy:

1. Mechanical split first, no behavior change.
2. Keep old `ExBootstrap*` names as wrappers.
3. Move call sites from wrappers to new APIs one profile at a time.
4. Delete compatibility wrappers after profiles are updated.

### P0. Move Policy Syscalls Out Of Ke

Current problem:

- `src/kernel/ke/user_bootstrap_syscall.c` handles user-visible policy:
  `SYS_READLINE`, `SYS_SPAWN_BUILTIN`, `SYS_WAIT_PID`, `SYS_SLEEP_MS`, `SYS_KILL_PID`.
- It includes `kernel/demo_shell.h`.
- This violates the project's own Ke/Ex story.

Target:

- Ke owns only the trap entry and safe mechanics:
  interrupt/trap registration, frame decoding, generic user-copy primitives, and no-return transition to thread exit.
- Ex owns syscall numbers, validation, object lookup, process control, and return values.

Concrete move:

- Keep `int 0x80` registration in Ke for now.
- Replace `KiDispatchBootstrapSyscall()` with a call into `ExDispatchSyscall(frame or nr/args)`.
- Move `readline/spawn/wait/sleep/kill/query_sysinfo/write/close/wait_one/exit` dispatch into Ex.
- Keep `SYS_RAW_*` only for legacy regression profiles and mark them as test-only.

### P0. Introduce Object Manager Lite

Current problem:

- Ex has the beginning of an object model, but it is embedded in bootstrap code.

Target concept:

Object Manager Lite is not a full NT object namespace. It is a small teaching abstraction:

- every user-visible kernel resource is an `EX_OBJECT`
- every object has a type, reference count, flags, and optional destroy routine
- user code sees integer handles, not kernel pointers
- handles carry rights
- stale handles are rejected through generation checks
- process teardown closes all process-owned handles

Initial object types:

- `Process`
- `Thread`
- `Console`
- `Input`
- `Event`
- `Timer` or `Sleepable`
- `ProgramImage`

Teaching value:

- Students can see the difference between an object, a handle, and a pointer.
- Rights checks become visible and testable.
- Teardown bugs can be explained as ownership graph bugs, not mysterious race behavior.

### P0. Replace Runtime Alias Table With Process/Thread Tables

Current problem:

- `gExBootstrapRuntimeAliases[4]` maps `KTHREAD*` to `EX_PROCESS*` and `EX_THREAD*`.
- This is a demo registry, not a process model.

Target:

- Ex owns a process table and a thread table.
- `EX_THREAD` has a pointer to its backing `KTHREAD`.
- `KTHREAD` should either carry a narrow opaque `OwnerContext` or be discoverable through an Ex registry keyed by
  `ThreadId`.
- Scheduler root resolution should ask a generic owner/address-space query, not a bootstrap callback.

Minimum target capacity:

- Keep static arrays if needed for teaching simplicity, but name them honestly:
  `EX_MAX_PROCESSES`, `EX_MAX_THREADS_PER_PROCESS`, `EX_MAX_HANDLES_PER_PROCESS`.
- Capacity limits are fine in a teaching OS when they are explicit contracts.

### P0. Build A Real Process Lifecycle

Current problem:

- A process is mostly a wrapper around bootstrap staging and address space.
- Exit status, parent/child relationship, foreground ownership, and wait semantics are scattered.

Target lifecycle:

```text
Created -> Ready -> Running -> Exiting -> Zombie -> Reaped
```

Minimum `EX_PROCESS` state:

- PID
- parent PID
- process state
- exit code
- address space
- main thread
- handle table
- image/program metadata
- children list or bounded child table
- wait/exit dispatcher object

Minimum process APIs:

- `ExCreateProcessFromImage`
- `ExStartProcess`
- `ExExitProcess`
- `ExWaitProcess`
- `ExKillProcess`
- `ExCloseProcess`
- `ExQueryProcessBasicInfo`

Teaching value:

- This is the bridge from "threads exist" to "processes exist".
- It lets `ps`, `kill`, `wait`, and shell foreground/background behavior become understandable.

### P0. Make User Images First-Class

Current problem:

- User programs are split into `.text` and `.rodata` binaries by makefile rules.
- Fixed bootstrap layout gives each user image one code page, one const page, one guard page, one stack page.
- `ProgramId` is hardcoded into ABI constants.

Target:

- Introduce `EX_USER_IMAGE`.
- In the first phase, `EX_USER_IMAGE` can still point to embedded code/const blobs.
- In the second phase, `EX_USER_IMAGE` should come from an ELF loader over an initrd or read-only image table.

Minimum `EX_USER_IMAGE` fields:

- name
- image kind: embedded split blob, embedded ELF, initrd ELF
- entry point
- load segments
- default stack size
- requested capabilities

Migration path:

1. Replace direct artifact bridge calls with `ExLookupProgramImage("hsh")`.
2. Generate builtin program registry entries from make variables or a manifest.
3. Load full ELF segments instead of only `.text` and `.rodata`.
4. Later add an initrd and make the registry point to files.

### P1. Create A TTY-Lite Layer

Current problem:

- Input is a single foreground-owner line lane.
- Console output is global.
- `readline` is a syscall policy sitting near Ke.

Target:

- A minimal terminal object:
  input line discipline, echo, foreground process group or foreground owner, output sink, EOF/control-key handling.
- Keep it single-terminal and single-AP.
- Do not build a full POSIX TTY.

Minimum APIs:

- `ExConsoleWrite(handle, buffer, len)`
- `ExConsoleReadLine(handle, buffer, cap)`
- `ExSetForegroundProcess(pid)`
- `ExGetForegroundProcess()`

Teaching value:

- Makes `hsh`, `calc`, and background `tick1s` explainable as terminal arbitration, not ad hoc input checks.

### P1. Separate Demo Profiles From Runtime Services

Current problem:

- `src/kernel/demo/demo_shell_runtime.c` implements spawn/wait/kill-ish behavior but exports `KeDemoShell*`.

Target:

- Runtime services move to Ex.
- Demo profiles become thin launchers and validators.
- `RunDemoShellDemo()` should call an Ex API to start initial user process, then wait or idle.

Concrete move:

- Move `KeDemoShellSpawnBuiltin` logic into `ExSpawnProgram`.
- Move child table into Ex process/child tracking.
- Keep `demo_shell` profile as a regression wrapper around `ExSpawnProgram("hsh")`.

### P1. Turn Sysinfo Into A Stable Contract

Current problem:

- Ke sysinfo is good.
- Ex sysinfo is mixed with text rendering and bootstrap ABI.

Target:

- Structured sysinfo is the stable ABI.
- Text sysinfo is a convenience layer for `hsh`.
- `ps`, `sysinfo`, and `memmap` should be built on structured classes when possible.

Recommended classes:

- `EX_SYSINFO_SYSTEM`
- `EX_SYSINFO_MEMORY`
- `EX_SYSINFO_PROCESS_LIST`
- `EX_SYSINFO_THREAD_LIST`
- `EX_SYSINFO_KVA_RANGES`
- `EX_SYSINFO_TIME`
- `EX_SYSINFO_BOOT`

Teaching value:

- This gives the project a `/proc`-like story without implementing a full filesystem first.

### P1. Refactor Build Program Definitions

Current problem:

- The makefile repeats nearly identical rules for each user program.
- Artifact bridge files are duplicated.

Target:

- A single user program list:

```make
USER_PROGRAMS := user_hello user_counter hsh calc tick1s fault_de fault_pf
```

- A template rule builds ELF/code/const artifacts.
- A generated C registry or one generic bridge exposes builtin images.

Teaching value:

- Adding a user program becomes a small lab exercise instead of makefile archaeology.

### P2. Introduce A Minimal Read-Only Initrd Or RomFS

Current problem:

- Programs are linked into the kernel as objects.
- There is no file namespace.

Target:

- Add a read-only initrd image with a tiny directory table.
- Bootloader loads it next to kernel or embeds it in ESP.
- Ex program registry can load `hsh`, `calc`, and `tick1s` from initrd.

This is not a full filesystem. It is enough to teach:

- files as named byte ranges
- loader reads executable bytes
- shell launches by name
- kernel image and user image are separate artifacts

### P2. Normalize Fault Handling

Current problem:

- User faults can terminate bootstrap-owned threads, but the semantics are still bootstrap-flavored.
- Fault demo programs exist and are valuable.

Target:

- User-mode exceptions should become Ex process/thread termination reasons.
- Kernel-mode exceptions remain fatal unless explicitly recoverable.
- `ps` or `wait` should be able to report "exited by page fault" or "exited by divide error".

Teaching value:

- Cleanly demonstrates protection, isolation, and process death without killing the kernel.

## 4. Redundant Or Deadline-Only Code To Retire

Do not delete these all at once. Retire them after replacement contracts exist.

### 4.1 Raw Bootstrap Syscalls

Keep temporarily:

- `SYS_RAW_WRITE`
- `SYS_RAW_EXIT`

Retire from normal user programs:

- They should remain only for low-level regression sentinels such as `user_hello`.
- The official user ABI should use `SYS_WRITE`, `SYS_EXIT`, and handle/capability APIs.

### 4.2 P1 Mailbox Gate As A Public Concept

Current:

- User programs wait for `KE_USER_BOOTSTRAP_STACK_MAILBOX_ADDRESS` to open.

Retire:

- Once user-mode timer/preemption proof is no longer needed in every program.

Replacement:

- Normal program entry should not require a magic mailbox.
- Keep a specific timer/preemption regression profile if needed.

### 4.3 Waitable Companion Thread

Current:

- `KiBootstrapWaitableCompanionThreadEntry()` exists to back a pilot wait handle.

Retire:

- Replace with a real dispatcher object owned by Ex.

### 4.4 Runtime Alias Capacity Four

Current:

- `EX_BOOTSTRAP_RUNTIME_ALIAS_CAPACITY = 4`.

Retire:

- Replace with Ex process/thread tables.

### 4.5 Demo Shell Runtime Control Plane

Current:

- `KeDemoShellSpawnBuiltin`
- `KeDemoShellWaitPid`
- `KeDemoShellKillPid`
- `KeDemoShellShouldTerminateCurrentThread`

Retire:

- Move semantics into Ex process manager.
- Keep demo wrappers only if they improve test readability.

### 4.6 Borrowing KTHREAD From Ex

Current:

- `ExBootstrapBorrowKernelThread()` exposes a `KTHREAD*` to demo code.

Retire:

- Use Ex waitable process/thread handles instead.

### 4.7 Hardcoded Builtin Program IDs In The ABI

Current:

- `KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_HSH`
- `KE_USER_BOOTSTRAP_BUILTIN_PROGRAM_CALC`
- etc.

Retire:

- Program names should be registry entries.
- Numeric IDs can remain internal implementation details.

### 4.8 Manual Artifact Bridges

Current:

- `hsh_artifact_bridge.c`, `calc_artifact_bridge.c`, `tick1s_artifact_bridge.c`, etc.

Retire:

- Replace with a generated builtin image table or initrd.

## 5. Feature Roadmap For A Teaching OS

### Stage 0: Stabilize The Baseline

Purpose:

- Freeze current behavior before refactoring.

Deliverables:

- `new_era_plan.md`
- current architecture map
- profile anchor index
- `make all` build sanity
- `demo_shell`, `user_input`, `user_dual` host/TCG evidence when touching user/process paths

Exit criteria:

- The team can explain what is demo scaffolding and what is intended architecture.

### Stage 1: Executive Lite

Purpose:

- Make Ex real.

Deliverables:

- Object Manager Lite
- handle table module
- process table
- thread table
- Ex syscall dispatcher
- process lifecycle states
- `ps` backed by Ex process/thread info

Exit criteria:

- `hsh` launches `calc`/`tick1s` through Ex process APIs.
- Ke syscall code no longer includes demo-shell policy.

### Stage 2: Program Loading

Purpose:

- Stop treating user programs as special kernel demo payloads.

Deliverables:

- `EX_USER_IMAGE`
- builtin image registry
- generic embedded image table
- minimal ELF segment loader
- argument passing: `argc/argv` or a compact argument block

Exit criteria:

- Adding a user program does not require a new artifact bridge C file.

### Stage 3: Terminal And Shell Semantics

Purpose:

- Turn `hsh` from demo shell into a tiny but coherent teaching shell.

Deliverables:

- TTY-lite object
- foreground process tracking
- background jobs
- `kill`
- `wait`
- clean `exit`
- line editing basics: backspace already exists, add Ctrl-C later if desired

Exit criteria:

- `hsh -> & tick1s -> ps -> calc -> kill <pid> -> exit` is a stable teaching path.

### Stage 4: Observability-First Teaching Tools

Purpose:

- Make invisible kernel concepts visible.

Deliverables:

- `sysinfo`
- `ps`
- `memmap`
- `uptime`
- `fault` demo command or programs
- structured Ex sysinfo ABI

Exit criteria:

- A student can run commands and observe memory, scheduling, process, and time state without reading kernel logs.

### Stage 5: Minimal Storage Story

Purpose:

- Teach file/program separation without building a huge filesystem.

Deliverables:

- read-only initrd or RomFS
- file table
- open/read/close on read-only files
- load program by path from initrd

Exit criteria:

- `hsh` can list a small directory and launch a named program.

### Stage 6: Course Packaging

Purpose:

- Turn the OS into teaching material.

Deliverables:

- lab guide for boot
- lab guide for PMM/VMM
- lab guide for scheduler
- lab guide for syscall/handle
- lab guide for process lifecycle
- architecture diagrams
- ADRs
- regression command cookbook

Exit criteria:

- New contributors can change one subsystem without first reverse-engineering the whole branch.

## 6. Classical OS Task Coverage Matrix

| Classical Topic | Current State | Teaching Target |
| --- | --- | --- |
| Boot | Strong UEFI loader and boot capsule | freeze boot contract, add overview doc |
| Physical memory | Strong PMM with stats | keep, add labs |
| Virtual memory | Good Ke KVA and process roots | add user layout allocator and ELF segments |
| Kernel heap | First-pass allocator plus pool | refine docs, keep simple |
| Threads | Good KTHREAD lifecycle | expose through Ex thread table |
| Scheduling | Priority-aware tickless RR | add `ps` and scheduling demos |
| Synchronization | Events, mutexes, semaphores, guards | keep as Ke labs |
| Processes | Bootstrap wrapper only | Ex process manager |
| Syscalls | Working but split across Ke/Ex/demo | Ex syscall table |
| Handles | Capability pilot | Object Manager Lite |
| User programs | Embedded split blobs | image registry, then ELF/initrd |
| Terminal I/O | Single foreground input lane | TTY-lite |
| File system | none | read-only initrd/RomFS |
| Fault handling | good demos, bootstrap flavor | Ex termination reasons |
| Observability | Ke sysinfo, Ex text sysinfo | structured Ex sysinfo plus commands |
| Regression | profile-driven QEMU logs | profile contracts and CI-ish scripts |

## 7. Concepts For Storytelling And "Drawing The Cake"

These are the concepts the project should introduce in docs, presentations, and future PR descriptions.

### 7.1 Executive Lite

Positioning:

- Not a full Windows NT executive.
- A small teaching executive that owns process, thread, object, handle, syscall, and program-image policy.

Why it helps:

- It gives Ex a real identity.
- It explains why user-visible services do not belong in Ke.

### 7.2 Mechanism, Policy, Contract

Definition:

- Ke provides mechanisms.
- Ex chooses policies.
- User ABI documents contracts.

Examples:

- Ke can wait on a dispatcher object; Ex decides that `wait_pid` waits on a process exit object.
- Ke can switch CR3; Ex decides which address space belongs to a process.
- Ke can copy bytes from a validated user range; Ex decides which syscall argument is valid.

### 7.3 Object Manager Lite

Definition:

- A minimal object/handle/rights model.

Why it helps:

- It explains handles, stale-handle rejection, teardown, refcounts, and resource ownership in one place.

### 7.4 Process Lifecycle As A State Machine

Definition:

- `Created -> Ready -> Running -> Exiting -> Zombie -> Reaped`.

Why it helps:

- It turns tricky teardown races into visible lifecycle errors.
- It gives `wait`, `kill`, `ps`, and fault termination a shared vocabulary.

### 7.5 Address Space As A Teaching Canvas

Definition:

- Every process has a visible low-half layout, and the kernel has visible high-half KVA arenas.

Why it helps:

- `memmap` can show textbook address-space diagrams using the live kernel.

### 7.6 Observability-First Kernel

Definition:

- Every major subsystem should expose a bounded, structured query surface.

Why it helps:

- A teaching OS should show its internal state.
- Debugging through `sysinfo`, `ps`, and `memmap` is more educational than only reading serial logs.

### 7.7 Profiles As Contracts

Definition:

- A demo/test profile is not merely a script. It is a contract with named log anchors and expected behavior.

Why it helps:

- It keeps refactors safe.
- It lets deadline-era demos become regression assets instead of dead code.

### 7.8 Vertical Slices, Not Feature Piles

Definition:

- Each milestone should connect boot/kernel/user/testing into one visible path.

Good examples:

- `hsh -> ps` proves process table and sysinfo.
- `hsh -> calc` proves foreground process and TTY.
- `hsh -> memmap` proves VMM observability.
- `fault_pf` proves user fault isolation.

### 7.9 Teaching Kernel, Not Production Kernel

Definition:

- Fixed capacities are acceptable when named and enforced.
- Single-AP is acceptable when documented.
- Read-only initrd is acceptable before a writable FS.
- Simple syscall ABI is acceptable before POSIX compatibility.

Why it helps:

- Prevents scope explosion.
- Makes the project honest and finishable.

## 8. Documentation Plan

Add these docs:

- `docs/architecture/overview.md`
  One-page map: Boot, Ke, Ex, User, Demos.
- `docs/architecture/ke-ex-boundary.md`
  What Ke may know, what Ex may know, and forbidden dependencies.
- `docs/architecture/ex-object-manager-lite.md`
  Object, handle, rights, lifetime, teardown.
- `docs/architecture/process-lifecycle.md`
  Process and thread states, wait/kill/exit/fault.
- `docs/architecture/syscall-abi.md`
  Register ABI, syscall table, error returns, user-copy rules.
- `docs/architecture/user-image-loader.md`
  Embedded image now, ELF/initrd later.
- `docs/adr/0001-keep-single-ap-teaching-scope.md`
- `docs/adr/0002-executive-lite-boundary.md`
- `docs/adr/0003-profile-contract-regression.md`

Update these docs:

- `Readme.md`
  Reframe MVP as completed baseline and point to New Era roadmap.
- `docs/design/ke-mm.md`
  Fix stale absolute links and add how Ex process roots use Ke mechanisms.
- `docs/apis/KeQuerySystemInformation.md`
  Clarify which classes are Ke-internal and which are surfaced through Ex.

## 9. Testing And Regression Plan

Core rule:

- Ex/process/syscall/teardown changes require both host and TCG logs for timing-sensitive profiles.

Required profile groups:

- Core scheduler:
  `schedule`
- User bootstrap compatibility:
  `user_hello`, `user_caps`
- Multi-process timing:
  `user_dual`
- Input and shell:
  `user_input`, `demo_shell`
- Fault isolation:
  `user_fault`, `pf_imported`, `pf_guard`, `pf_fixmap`, `pf_heap`
- Lifecycle stress:
  `kthread_pool_race`

Add future profiles:

- `ex_handle`
  stale handle, rights rejection, close-all-on-exit.
- `ex_process`
  spawn, wait, exit code, zombie/reap.
- `ex_kill`
  background process kill and shell clean exit.
- `ex_fault`
  page fault terminates user process, not kernel.
- `loader_elf`
  ELF image with multiple loadable segments.

Regression artifacts:

- Keep serial logs or summarized anchors in `docs/fix-report-*` only for important bugs.
- For ordinary profile updates, maintain a `docs/regression-profiles.md` table.

## 10. Suggested First PR Sequence

### PR 1: Document Boundaries

No code behavior change.

- Add architecture docs.
- Add ADR skeleton.
- Move this plan into the README roadmap.

### PR 2: Mechanical Ex Split

No behavior change.

- Split `ex_bootstrap.c` and `ex_bootstrap_adapter.c` into modules.
- Preserve existing public APIs.
- Run current user profiles.

### PR 3: Ex Syscall Dispatcher

Behavior-preserving, boundary-changing.

- Ke trap calls Ex dispatcher.
- Move policy syscall switch to Ex.
- Keep raw syscalls as compatibility.

### PR 4: Object Manager Lite

Mostly internal behavior.

- Central object header/type/destroy ops.
- Central handle table.
- Existing stdout/wait/process/thread handles move onto it.

### PR 5: Process Table And PID

Visible feature.

- Add Ex process table.
- Introduce PID independent from `KTHREAD::ThreadId`.
- Add `ps` command.

### PR 6: Program Registry

Visible feature and build cleanup.

- Replace hardcoded artifact bridge use with registry lookup.
- Generate or centralize builtin image definitions.

### PR 7: Shell Lifecycle

Visible feature.

- Move demo shell runtime control into Ex.
- Implement clean `kill` and `exit` path.
- Update `demo_shell.plan`.

## 11. Non-Goals

For the new era, explicitly do not promise:

- SMP
- POSIX compatibility
- writable disk filesystem
- demand paging
- copy-on-write fork
- network stack
- GUI
- full dynamic linker
- security model beyond teaching-grade isolation and handle rights

These can be mentioned as future research directions, but they should not block the teaching OS target.

## 12. Final Positioning

The strongest positioning for HimuOS is:

> HimuOS is an observability-first x86_64 teaching macro-kernel. It uses a modern UEFI boot path, a mechanism/policy split
> inspired by NT-style kernels, and small vertical user-mode slices to make classical OS concepts visible: memory,
> scheduling, processes, syscalls, handles, faults, and terminal I/O.

This positioning is honest. It does not pretend the OS is complete. It also does not undersell the work. The goal is not
to outgrow the teaching scope; the goal is to make the scope internally coherent, explainable, and pleasant to maintain.

