# HimuOS New Era Next Plan

> Date: 2026-04-30  
> Purpose: finish the New Era cleanup by removing transitional Bootstrap-era compromises while preserving existing behavior.

## 0. North Star

The previous New Era work turned the deadline demo branch into a working teaching kernel with a real Executive Lite
direction. The next step is not to add more features. The next step is to make the system cleaner, more honest, and less
temporary.

The end state of this plan is:

- user mode is a first-class runtime, not a bootstrap experiment
- Ex owns process, thread, object, handle, syscall, program, and user-visible policy
- Ke owns mechanisms and does not depend on Ex bootstrap ABI details
- demo profiles prove contracts, but do not define runtime services
- normal user programs do not wait on phase gates or use raw bring-up syscalls
- no compatibility layer, public API, document, or active runtime path remains shaped around Bootstrap-era naming or
  behavior

In short:

> When this plan is complete, "Bootstrap" should mean only early machine boot. It should not appear as a user-runtime
> compromise, compatibility surface, or architectural excuse.

## 1. Scope

This plan is deliberately a cleanup plan, not a feature roadmap.

In scope:

- rename and reshape transitional concepts into permanent runtime contracts
- remove raw bring-up paths from normal user programs
- collapse duplicate process/thread tracking into Ex-owned tables
- make process lifecycle, waits, foreground ownership, and fault termination Ex concepts
- split broad ABI surfaces into smaller stable contracts
- keep existing demos and user-visible behavior passing throughout the transition

Out of scope:

- a full filesystem
- POSIX compatibility
- multi-core scheduling
- a full TTY/job-control implementation
- changing syscall numbers or user-visible behavior just for aesthetic reasons
- expanding the shell beyond the behavior already used by the current profiles

## 2. Guiding Rules

1. Preserve behavior first.
   Existing profiles are contracts. Refactors should keep their visible behavior and log anchors unless a contract is
   explicitly retired and replaced.

2. Replace before deleting.
   A temporary path is removed only after the permanent Ex or Ke contract exists and profiles have moved to it.

3. Names must tell the truth.
   If a concept is permanent, it must not be called Bootstrap. If it is temporary, it must have a deletion condition.

4. Ke must not learn policy.
   Ke can trap, copy, schedule, switch address spaces, wait, and report mechanism state. Ex decides what those mechanisms
   mean to user programs.

5. Fixed capacities are acceptable when explicit.
   A teaching OS can use bounded tables, but the limits must be named as product constraints rather than hidden demo
   artifacts.

## 3. Phase A: Freeze The Cleanup Baseline

Goal:

Make the current post-New-Era state measurable before deeper cleanup starts.

Work:

- record which profiles are official contracts
- mark which profiles are legacy bring-up sentinels
- update architecture notes so they describe the current implementation rather than the old migration target
- define a short "Bootstrap debt index" listing every remaining Bootstrap-era runtime concept

Expected result:

- the team can point to every remaining historical compromise
- each compromise has an owner phase and a deletion condition
- no cleanup starts from vague discomfort; every removal has a named replacement

Exit criteria:

- `demo_shell`, `user_input`, `user_dual`, and `user_fault` remain the timing-sensitive safety net
- documentation clearly distinguishes early boot from user runtime
- there is an explicit list of runtime paths that must stop using Bootstrap terminology

## 4. Phase B: Split The User ABI Into Stable Contracts

Goal:

Stop using one broad bootstrap ABI header as the shared home for unrelated contracts.

Work:

- separate syscall ABI, sysinfo ABI, user image layout, capability seed, and regression anchors into focused public
  contracts
- keep the existing numeric syscall contract stable
- make user programs include stable user-runtime ABI names
- move test-only anchors and raw bring-up details out of the normal user ABI surface

Expected result:

- normal user code reads like it is using a real HimuOS user ABI
- raw bring-up constants are visibly test-only
- Ke headers no longer depend on Ex bootstrap ABI names for unrelated mechanism constants

Exit criteria:

- normal user programs do not include a Bootstrap-named ABI header
- Ex-facing syscall and sysinfo contracts have permanent names
- compatibility includes, if temporarily present during this phase, have a documented deletion date within this plan

## 5. Phase C: Rename Bootstrap Runtime Hooks Into User Runtime Hooks

Goal:

Make the Ke-to-Ex callback boundary describe the permanent user runtime instead of the original bring-up path.

Work:

- rename callback concepts around user runtime ownership, address-space root resolution, user entry, finalization, timer
  observation, and user fault handoff
- keep the semantics narrow: Ke asks questions or reports events; Ex owns policy
- remove Bootstrap wording from scheduler, timer, and exception handoff descriptions

Expected result:

- the scheduler can dispatch a user-owned thread without knowing that the first implementation came from a bootstrap path
- user-mode faults are routed as user-runtime events, not bootstrap exceptions
- future process work can plug into the same hook contract without another rename

Exit criteria:

- Ke public headers do not expose Bootstrap-named runtime callback types
- scheduler and exception code describe user runtime ownership rather than bootstrap ownership
- the old callback registration surface is deleted, not merely wrapped forever

## 6. Phase D: Make Ex Process And Thread Tables The Runtime Source Of Truth

Goal:

Replace alias registries and child-control tables with a single Ex-owned view of live processes and threads.

Work:

- introduce explicit Ex process and thread tables
- make PID/TID ownership Ex-owned and independent from internal KTHREAD identity
- track parent, main thread, state, exit status, termination reason, and foreground metadata in Ex
- make process/thread lookup flow through Ex tables rather than ad hoc runtime aliases

Expected result:

- `ps`, `wait`, `kill`, fault handling, and foreground restoration all read from the same model
- there is one process lifecycle vocabulary
- a process can be explained without pointing at a borrowed kernel thread pointer

Exit criteria:

- no process-control path stores a borrowed `KTHREAD *` as its primary process identity
- runtime alias tables are gone
- child process tracking is part of the Ex process model rather than a separate shell-control structure

## 7. Phase E: Replace Pilot Wait Objects With Real Ex Waitables

Goal:

Remove the companion-thread artifact used to make early wait handles work.

Work:

- give Ex process and thread objects real waitable completion state
- make process wait and generic wait-handle behavior use that state
- keep the existing user-facing wait behavior stable

Expected result:

- waiting on a process means waiting on the process object
- waiting on a thread means waiting on the thread object
- no hidden helper thread exists solely to manufacture waitability

Exit criteria:

- no companion thread exists in the waitable-object implementation
- wait handles remain generation-checked and rights-checked
- teardown is simpler because waitability is owned by the object being waited on

## 8. Phase F: Move All Demo Profiles Onto Permanent Ex APIs

Goal:

Stop letting early profiles depend on transitional launch and join paths.

Work:

- migrate user-facing profiles to `spawn`, `wait`, `kill`, and foreground operations owned by Ex
- keep raw bring-up profiles only where they prove a low-level contract that is still intentionally supported
- remove direct use of pre-runtime Ex create/start/borrow calls from ordinary demos

Expected result:

- demo profiles launch scenarios rather than constructing runtime internals
- user input and dual-process profiles prove the same process model that `demo_shell` uses
- remaining low-level sentinels are obviously tests, not examples of normal program launch

Exit criteria:

- `demo_shell`, `user_input`, `user_dual`, and `user_fault` all use permanent Ex runtime APIs
- no ordinary demo profile borrows a kernel thread from Ex
- raw bring-up code is isolated to explicitly named sentinel profiles

## 9. Phase G: Retire Phase Gates And Raw Syscalls From Normal Userspace

Goal:

Make normal user program startup look like a real runtime contract.

Work:

- remove phase-mailbox waiting from normal user programs
- remove raw write and raw exit from normal user programs
- keep a small raw sentinel only if it still provides unique regression value
- ensure all ordinary programs use formal Ex syscalls for write, exit, read, spawn, wait, sleep, kill, and sysinfo

Expected result:

- `hsh`, `calc`, `tick1s`, fault demos, and ordinary user test programs enter directly through the user runtime
- raw syscalls no longer shape the teaching story
- phase gates become a retired bring-up technique, not a runtime dependency

Exit criteria:

- normal userspace has no phase-gate wait
- normal userspace has no raw syscall wrapper
- any remaining raw syscall profile is named and documented as a low-level sentinel
- if the sentinel no longer provides unique value, raw syscalls are deleted entirely

## 10. Phase H: Make Structured Sysinfo The Contract

Goal:

Keep observability strong while separating data contracts from display formatting.

Work:

- keep structured sysinfo classes as the stable kernel-to-user contract
- treat text sysinfo as a convenience layer, not the core ABI
- move formatting toward user space where practical
- keep shell-visible output stable while the underlying contract becomes cleaner

Expected result:

- `sysinfo`, `memmap`, and `ps` are views over structured runtime data
- Ex owns process and thread observability
- Ke contributes mechanism snapshots without formatting user-facing policy views

Exit criteria:

- process and thread lists come from Ex process/thread tables
- text rendering is no longer required to understand the ABI
- sysinfo docs separate stable structures from presentation helpers

## 11. Phase I: Delete Compatibility Surfaces

Goal:

Finish the cleanup by removing every transitional name, wrapper, and document exception kept only for migration.

Work:

- delete Bootstrap-named user-runtime headers and wrappers
- delete compatibility source files whose only purpose was preserving old call shapes
- remove stale comments that describe current runtime behavior as temporary bootstrap behavior
- update regression docs so retired paths are no longer recommended workflows

Expected result:

- the codebase tells one story
- new contributors see Boot, Ke, Ex, User, and Demo as separate permanent concepts
- there is no hidden historical path that must be understood before touching user runtime code

Exit criteria:

- searching active source headers for Bootstrap finds only true early-boot code or historical documentation
- no Ex public runtime API is named Bootstrap
- no Ke runtime callback, scheduler path, syscall path, or user-fault path is named Bootstrap
- no normal user program depends on a Bootstrap-named constant, helper, or ABI
- compatibility wrappers are deleted rather than kept as permanent comfort blankets

## 12. Final Acceptance Criteria

This plan is complete only when all of the following are true:

- `make all` succeeds
- `make test list` reports the supported profile set clearly
- timing-sensitive profiles have fresh host and TCG evidence when touched
- `demo_shell` still proves the interactive vertical slice
- `user_input` still proves foreground input ownership
- `user_dual` still proves concurrent user processes and teardown
- `user_fault` still proves user fault isolation and recovery to shell
- no ordinary user path uses raw syscalls
- no ordinary user path waits on a phase mailbox
- no demo profile constructs user runtime internals directly
- no process-control path exposes or depends on borrowed `KTHREAD *`
- Ex process/thread tables are the source of truth for process identity, wait, kill, fault, and sysinfo
- Bootstrap terminology is gone from active user-runtime APIs and mechanisms

## 13. Completion Statement

The intended final architecture is not "Bootstrap with better names." It is a cleaned-up teaching operating system:

- Boot loads and hands off
- Ke provides mechanisms
- Ex owns runtime policy
- user programs consume a stable ABI
- demos prove contracts

After this plan is finished, any remaining Bootstrap reference must either describe the actual bootloader/early boot path
or be removed. There should be no Bootstrap compatibility mode, no Bootstrap runtime facade, no Bootstrap syscall story,
and no Bootstrap-shaped compromise left in the user-mode system.
