# Bootstrap Runtime Debt Index

This index covers runtime and public-ABI debt only. True early boot under
`src/boot/v2` is not debt for this plan. Existing regression log anchors stay
fixed until an explicit replacement contract retires them.

| Debt | Current surface | Owner phase | Deletion condition |
| --- | --- | --- | --- |
| Runtime alias registry as identity glue | `src/kernel/ex/runtime_alias.c` and the `ExBootstrapLookupRuntime*()` helpers still map runtime process/thread identity through temporary alias slots keyed by `KTHREAD *`. | Phase D | Ex process/thread tables become the only runtime identity source; alias slots and bootstrap lookup helpers are gone. |
| Child control table stores borrowed `KTHREAD *` state | `src/kernel/ex/process_control.c` keeps `gExChildProcessTable` with parent/child metadata, kill state, foreground restore data, and a borrowed `KernelThread` wait anchor. | Phase D | Parent/child, main-thread, state, and foreground metadata live in Ex process/thread tables instead of an ad hoc child table. |
| Waitability depends on joinable kernel threads and pilot wait objects | `src/kernel/ex/thread.c` still seeds waitability through `EX_BOOTSTRAP_THREAD_CREATE_FLAG_SEED_WAIT_OBJECT` and `KiCreateWaitablePilotHandle()`, while `ExWaitProcess()` and `ExKillProcess()` still join a borrowed `KernelThread`. | Phase E | Process/thread objects own real waitable completion state; no companion wait object or borrowed join thread remains. |
| Contract-profile launch paths still depend on bootstrap helpers | `src/include/kernel/ex/program.h` still exports `ExProgramBuildBootstrapCreateParams()`, `src/kernel/ex/process_control.c` still implements the `demo_shell` / `user_fault` Ex control plane through `ExBootstrap*` helpers, and `src/kernel/demo/user_dual.c` plus `src/kernel/demo/user_input.c` still call those helpers directly; `user_input` also calls `ExBootstrapBorrowKernelThread()`. | Phase F | Contract profiles use permanent Ex runtime APIs internally and externally for launch, wait, kill, and foreground control. |
| Normal userspace still depends on phase gates and raw bootstrap helpers | `HoUserWaitForP1Gate()` now lives in explicit sentinel-only `src/user/libsys_bringup.h` and remains included by `tick1s`, `fault_de`, `fault_pf`, `user_counter`, and `user_hello`; `HoUserRaw*()` is no longer in `src/user/libsys.h`, but remains in `src/user/libsys_bringup.h` for `user_hello`. | Phase G | Ordinary userspace enters directly and uses only formal Ex syscalls; any remaining raw sentinel is explicit and documented, or raw helpers are deleted entirely. |
| Remaining bootstrap-named runtime headers and helpers | Active runtime headers still include `src/include/kernel/ex/ex_bootstrap.h`, `src/include/kernel/ex/ex_bootstrap_adapter.h`, `src/include/kernel/ex/ex_process.h`, `src/include/kernel/ex/ex_thread.h`, `src/include/kernel/ex/program.h` (`ExProgramBuildBootstrapCreateParams()`), and `src/include/kernel/ke/user_bootstrap.h`. In active runtime source, `src/kernel/ex/user_runtime_bridge.c` still defines the transitional `ExBootstrapAdapter*()` helper family; `src/kernel/ex/syscall.c` still carries the Bootstrap-named exit/dispatch path via `KiAbortBootstrapExit()`, `KiPrepareBootstrapExit()`, `KiPrepareBootstrapKillExit()`, and `ExBootstrapAdapterDispatchSyscall()`; `src/kernel/ex/process.c` still exports `ExBootstrapInitializeProcessObject()`, `ExBootstrapTeardownProcessPayload()`, and `ExBootstrapReleaseProcess()`; `src/kernel/ex/thread.c` still exports `ExBootstrapCleanupWaitableBacking()`, `ExBootstrapInitializeThreadObject()`, and `ExBootstrapBorrowKernelThread()`; `src/kernel/ex/object.c` still exports `ExBootstrapInitializeStdoutServiceObject()`, `ExBootstrapInitializeWaitableObject()`, `ExBootstrapReleaseStdoutServiceOwner()`, and `ExBootstrapReleaseWaitableObjectOwner()`; and `src/kernel/ex/handle.c` still depends on `ExBootstrapReleaseProcess()`, `ExBootstrapReleaseThread()`, `ExBootstrapCleanupWaitableBacking()`, and `ExBootstrapIsRuntimeAliasObject()`. | Phase I | Searching active runtime headers and source for `Bootstrap` finds only true early-boot code or historical docs; compatibility wrappers and bootstrap-named helper families are deleted rather than kept. |

## Retired During Phase C

- The Ke-to-Ex runtime callback surface was renamed from bootstrap callbacks
  to user-runtime hooks. The permanent contract now lives in
  `src/include/kernel/ke/user_runtime_hooks.h` and
  `src/kernel/ke/user_runtime_hooks.c`.
- Scheduler dispatch, timer observation, and IDT user-fault handoff now call
  `KeRegisterUserRuntimeHooks()` / `KiGetUserRuntime*Hook()` names.
- The Ex hook implementation moved from `src/kernel/ex/bootstrap_compat.c` to
  `src/kernel/ex/user_runtime_bridge.c`; the remaining `ExBootstrapAdapter*()`
  helper names are tracked as Phase I debt, not as the Ke hook contract.

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

- `user_hello` preserves raw syscall, phase-gate, and teardown anchors.
- `user_caps` preserves capability-seed, stdout-handle, and pilot wait coverage.

They remain valid only until later phases replace their unique regression value
or retire the underlying bring-up path.
