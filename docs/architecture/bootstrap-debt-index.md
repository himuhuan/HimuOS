# Bootstrap Runtime Debt Index

This index covers runtime and public-ABI debt only. True early boot under
`src/boot/v2` is not debt for this plan. Existing regression log anchors stay
fixed until an explicit replacement contract retires them.

No active runtime debt remains in this index. Searching active source and
headers for `Bootstrap` should now find only true early-machine boot state,
early allocator/MM comments, or historical documentation.

## Retired During Phase I

- `src/include/kernel/ex/ex_bootstrap.h` became
  `src/include/kernel/ex/ex_runtime.h`, and `src/kernel/ex/ex_bootstrap.c`
  became `src/kernel/ex/runtime.c`.
- `src/include/kernel/ex/ex_bootstrap_adapter.h` became
  `src/include/kernel/ex/ex_user_runtime.h`; the empty
  `src/kernel/ex/ex_bootstrap_adapter.c` compatibility unit was deleted.
- `src/include/kernel/ke/user_bootstrap.h`,
  `src/kernel/ke/user_bootstrap.c`, `src/kernel/ke/user_bootstrap_syscall.c`,
  and `src/arch/amd64/user_bootstrap.asm` moved to `user_mode` names.
- Ex process/thread/object/runtime helpers were renamed from Bootstrap helper
  families to runtime/user-runtime helper families.
- `ExProgramBuildBootstrapCreateParams()` was replaced by
  `ExProgramBuildRuntimeCreateParams()`.
- The compatibility `ExBootstrapQueryCurrentProcessId()` wrapper was deleted.
- Runtime comments, shell plans, and regression docs no longer recommend
  Bootstrap-named runtime paths.

## Retired During Phase G

- `tick1s`, `fault_de`, `fault_pf`, and `user_counter` now include
  `src/user/libsys.h` and enter directly without `HoUserWaitForP1Gate()`.
- Normal userspace no longer includes `src/user/libsys_bringup.h`, calls
  `HoUserRaw*()`, or waits on the P1 mailbox.
- `user_caps` is the only remaining raw syscall / phase-gate sentinel. The
  `user_hello` payload now runs on `src/user/libsys.h` and exits through
  `EX_USER_SYS_EXIT`.

## Retired During Phase F

- `user_dual` now launches `user_hello` and `user_counter` through
  `ExSpawnProgram()` and waits for both with `ExWaitProcess()`.
- `user_input` now launches `hsh` and `calc` through `ExSpawnProgram()`,
  performs foreground handoff through `ExSetForegroundProcess()`, and waits for
  both child processes with `ExWaitProcess()`.
- Contract demo profiles no longer call `ExBootstrapCreateProcess()`,
  `ExBootstrapCreateThread()`, `ExBootstrapStartThread()`, or
  `ExBootstrapBorrowKernelThread()` directly.

## Retired During Phase E

- Pilot wait objects were removed. `EX_PROCESS` and `EX_THREAD` now own
  embedded completion events, and `SYS_WAIT_ONE` resolves wait-right handles to
  process or thread objects directly.
- `ExWaitProcess()` and `ExKillProcess()` no longer borrow a process main
  backing `KTHREAD` or call `KeThreadJoin()`. They retain the child process,
  wait on its Ex completion state, restore foreground ownership, and consume
  the completed process table entry.
- Normal Ex-spawned user threads are detached kernel threads; the idle reaper
  finalizes their user-runtime resources and signals Ex process completion.
- The `user_caps` sentinel now proves a valid seeded process wait handle by
  polling it for `EC_TIMEOUT`; it no longer depends on a pre-completed pilot
  companion thread.

## Retired During Phase D

- The runtime alias registry was replaced by `src/kernel/ex/runtime_table.c`.
  `ExRuntimeLookup*()`, `ExRuntimeCapture*()`, and
  `ExRuntimeIsPublishedObject()` are now the active runtime lookup surface.
- The process-control child table was deleted. Parent PID, child PID, main TID,
  kill request, termination state, exit status, termination reason, foreground
  ownership, and foreground restore metadata now live in `EX_PROCESS` /
  `EX_THREAD` state published through the runtime tables.
- `ps`, `wait`, `kill`, fault termination, and foreground restoration now read
  from the same Ex process/thread model.

## Retired During Phase C

- The Ke-to-Ex runtime callback surface was renamed from bootstrap callbacks
  to user-runtime hooks. The permanent contract now lives in
  `src/include/kernel/ke/user_runtime_hooks.h` and
  `src/kernel/ke/user_runtime_hooks.c`.
- Scheduler dispatch, timer observation, and IDT user-fault handoff now call
  `KeRegisterUserRuntimeHooks()` / `KiGetUserRuntime*Hook()` names.
- The Ex hook implementation moved from `src/kernel/ex/bootstrap_compat.c` to
  `src/kernel/ex/user_runtime_bridge.c`.

## Retired During Phase B

- The monolithic bootstrap ABI umbrella `src/include/kernel/ex/ex_bootstrap_abi.h`
  was deleted. Its contents now live in focused contracts:
  `src/include/kernel/ex/user_syscall_abi.h`,
  `src/include/kernel/ex/user_sysinfo_abi.h`,
  `src/include/kernel/ex/user_image_abi.h`,
  `src/include/kernel/ex/user_capability_abi.h`,
  `src/include/kernel/ex/user_bringup_sentinel_abi.h`, and
  `src/include/kernel/ex/user_regression_anchors.h`.
- `src/user/libsys.h` includes only the stable user-runtime ABI contracts.
  Raw bring-up helpers and P1 mailbox waits moved to `src/user/libsys_bringup.h`.
- `src/include/kernel/ke/input.h` owns its mechanism capacity directly instead
  of including an Ex bootstrap ABI header for `KE_INPUT_LINE_CAPACITY`.

## Legacy Bring-Up Sentinels Intentionally Kept During Phase A

- `user_caps` preserves capability-seed, stdout-handle, stale-handle rejection,
  and process wait-handle timeout coverage.

It remains valid only until later phases replace its unique regression value or
retire the underlying bring-up path.
