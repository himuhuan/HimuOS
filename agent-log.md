# P1 Agent Log

## Scope

- Change: `introduce-demo-shell-input-lane`
- Goal: implement `docs/todo.md` P1 as a bounded demo-shell input lane
- Coordination mode: main agent as coordinator, serial task execution, `host` / `tcg` dual validation, reviewer-fix loop before final delivery

## Constraints

- Keep the implementation minimal and strictly bounded to P1
- Do not widen the scope into a full shell, full `stdin` / `tty`, job control, filesystem, PATH, generic loader, or kill semantics
- Runtime input source stays on QEMU PS/2 keyboard only
- Supported keys remain bounded to printable characters, `Enter`, and `Backspace`
- OpenSpec artifacts under `openspec/changes/introduce-demo-shell-input-lane` are intentionally git-ignored and were updated directly during the workflow
- `docs/todo.md` is a user-side tracked change and was intentionally excluded from this delivery and its commit

## OpenSpec Flow

- Used the default planning agent to turn `docs/todo.md` P1 into a new OpenSpec change: `introduce-demo-shell-input-lane`
- Created proposal, design, delta specs, and `tasks.md` under that change
- Fixed the serial task structure as:
  - `1.x` Kernel Input Lane
  - `2.x` Foreground Read Contract
  - `3.x` P1 User Profile
  - `4.x` Scripted Validation
- Applied implementation strictly in task order, then revalidated, then sent the whole batch through reviewer

## Implementation Summary

### Stage 1-2

- Added a bounded runtime input subsystem under `src/kernel/ke/input/`
- Added the QEMU PS/2 keyboard path through:
  - `src/drivers/input/ps2_keyboard_driver.c`
  - `src/include/drivers/input/ps2_keyboard_driver.h`
  - `src/kernel/ke/input/sinks/ps2_keyboard_sink.c`
  - `src/include/kernel/ke/sinks/input_sink.h`
- Initialized runtime keyboard input in `src/kernel/init/init.c`
- Added `SYS_READLINE`, copyout support, and `HoUserReadLine()` through:
  - `src/include/kernel/ex/ex_bootstrap_abi.h`
  - `src/include/kernel/ke/user_bootstrap.h`
  - `src/kernel/ke/user_bootstrap_syscall.c`
  - `src/user/libsys.h`

### Stage 3

- Added minimal compiled userspace payloads:
  - `src/user/hsh/main.c`
  - `src/user/calc/main.c`
- Added embedded artifact bridges:
  - `src/kernel/demo/hsh_artifact_bridge.c`
  - `src/kernel/demo/calc_artifact_bridge.c`
- Added the `user_input` regression profile in `src/kernel/demo/user_input.c`
- Extended Ex bootstrap threading just enough for kernel-side coordination:
  - joinable bootstrap thread flag
  - thread-id query helper
  - kernel-thread borrow helper

### Stage 4

- Added headless sendkey support:
  - `scripts/qemu_sendkeys.py`
  - `scripts/input_plans/user_input.plan`
- Extended `scripts/qemu_capture.sh` and `makefile` so the existing capture-first workflow can optionally create a QEMU monitor socket and run the sendkey helper against it

## Build And Validation

### Build

Ran:

```bash
make clean && bear -- make all BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT
```

Result:

- Build completed successfully for the `test-user_input` flavor

### Host Validation

Ran:

```bash
BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT \
QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/user_input.plan \
bash scripts/qemu_capture.sh 25 /tmp/himuos-user-input-host.log
```

Key anchors in `/tmp/himuos-user-input-host.log`:

- `foreground -> hsh` at line 93
- `foreground -> calc` at line 165
- first `SYS_READLINE succeeded bytes=8 thread=1` at line 167
- `SYS_READLINE rejected thread=1` at line 182
- `[HSH] handoff` at line 184
- second `SYS_READLINE succeeded bytes=5 thread=2` at line 224
- `[CALC]` at line 225
- `foreground owner=0` at line 237

Interpretation:

- `hsh` read the first full line `& tick1s`
- foreground switched to a live `calc`
- the old `hsh` reader observed the handoff as a rejected second `readline`
- `calc` then read the second full line `3 4 +`
- foreground ownership was cleared at the end

### TCG Validation

Ran:

```bash
BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT \
QEMU_CAPTURE_MODE=tcg QEMU_SENDKEY_PLAN=scripts/input_plans/user_input.plan \
bash scripts/qemu_capture.sh 25 /tmp/himuos-user-input-tcg.log
```

Key anchors in `/tmp/himuos-user-input-tcg.log`:

- `foreground -> hsh` at line 92
- first `SYS_READLINE succeeded bytes=8 thread=1` at line 133
- `foreground -> calc` at line 155
- `SYS_READLINE rejected thread=1` at line 164
- `[HSH] handoff` at line 168
- second `SYS_READLINE succeeded bytes=5 thread=2` at line 192
- `[CALC]` at line 196
- `foreground owner=0` at line 211

Interpretation:

- The same live handoff contract reproduced on `tcg`
- Both runs captured the intended first line `& tick1s` and second line `3 4 +`

## Reviewer Findings And Fixes

### Reviewer Pass 1

The first reviewer pass raised three blockers:

1. Foreground handoff could leave the old reader blocked forever
2. `user_input` only proved sequential readers, not a live `hsh` / `calc` handoff
3. `qemu_capture.sh` could still hide sendkey helper failure behind timeout `124`

### Fixes Applied

- Changed `KeInputWaitForForegroundLine()` to re-check ownership on a bounded wait cadence instead of sleeping forever on the old event
- Added `KeInputGetCompletedReadCount()` so the kernel-side controller can switch foreground after the first completed read while both user threads are still alive
- Changed `user_input` to start both `hsh` and `calc`, then hand off from a live `hsh` to a live `calc`
- Changed `hsh` to issue a second `readline`, expect `-EC_INVALID_STATE`, and emit `[HSH] handoff`
- Changed `calc` to tolerate early `-EC_INVALID_STATE` until it becomes foreground
- Changed `scripts/qemu_capture.sh` to surface `SENDKEY_STATUS` before the watchdog timeout exit path
- Updated the sendkey plan to wait for `[HSH] handoff` before injecting the second line

### Reviewer Pass 2

- Re-review found no remaining blocking issues
- Residual risk is limited to noisy log interleaving under heavy `tcg` scheduling, but the correctness contract is now proven by the captured anchors

## Commit

- Commit: `b401651`
- Message: `Add demo-shell P1 input lane`

## Git State

- This delivery is committed
- Current working tree still shows only one unrelated tracked modification:
  - `docs/todo.md`
- That user-side change was intentionally not staged and not included in `b401651`

## Tracked Files In This Delivery

- `makefile`
- `scripts/qemu_capture.sh`
- `scripts/qemu_sendkeys.py`
- `scripts/input_plans/user_input.plan`
- `src/include/kernel/ex/ex_bootstrap.h`
- `src/include/kernel/ex/ex_bootstrap_abi.h`
- `src/include/kernel/ex/ex_thread.h`
- `src/include/kernel/ke/input.h`
- `src/include/kernel/ke/sinks/input_sink.h`
- `src/include/kernel/ke/user_bootstrap.h`
- `src/include/drivers/input/ps2_keyboard_driver.h`
- `src/drivers/input/ps2_keyboard_driver.c`
- `src/kernel/demo/demo.c`
- `src/kernel/demo/demo_internal.h`
- `src/kernel/demo/hsh_artifact_bridge.c`
- `src/kernel/demo/calc_artifact_bridge.c`
- `src/kernel/demo/user_input.c`
- `src/kernel/ex/ex_bootstrap.c`
- `src/kernel/init/init.c`
- `src/kernel/ke/input/input.c`
- `src/kernel/ke/input/sinks/ps2_keyboard_sink.c`
- `src/kernel/ke/user_bootstrap_syscall.c`
- `src/user/libsys.h`
- `src/user/hsh/main.c`
- `src/user/calc/main.c`
- `agent-log.md`
