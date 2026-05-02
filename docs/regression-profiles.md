# Regression Profiles

This index tracks HimuOS profile-driven regression for the cleanup plan. It
records which profiles are official cleanup contracts, which profiles are
targeted mechanism sentinels, and which anchors prove the expected behavior.

## Phase A Contract Split

Official cleanup contracts:

- `demo_shell`: interactive vertical slice for shell, sysinfo, spawn/wait/kill,
  and foreground ownership.
- `user_input`: foreground handoff and input ownership safety net.
- `user_dual`: concurrent compiled userspace and teardown safety net.
- `user_fault`: user fault isolation and recovery-to-shell safety net.

These four profiles remain the timing-sensitive safety net. They now launch
through the permanent Ex process-control surface.

Legacy raw/P1 bring-up sentinels:

- None. The raw syscall dispatcher, P1 mailbox, and bring-up helper ABI were
  retired during New-Era Clean Phase C.

Targeted mechanism sentinels:

- `schedule`
- `kthread_pool_race`
- `guard_wait`
- `owned_exit`
- `irql_wait`
- `irql_sleep`
- `irql_yield`
- `irql_exit`
- `pf_imported`
- `pf_guard`
- `pf_fixmap`
- `pf_heap`

## Common Workflow

Build a profile explicitly before capture:

```bash
make clean
bear -- make all BUILD_FLAVOR=<flavor> HO_DEMO_TEST_NAME=<profile> HO_DEMO_TEST_DEFINE=<define>
```

Capture with:

```bash
BUILD_FLAVOR=<flavor> HO_DEMO_TEST_NAME=<profile> HO_DEMO_TEST_DEFINE=<define> \
    bash scripts/qemu_capture.sh <seconds> /tmp/himuos-<profile>.log
```

Use `QEMU_CAPTURE_MODE=host` for KVM/host evidence and
`QEMU_CAPTURE_MODE=tcg` for emulated evidence. User/process profiles that touch
teardown, foreground input, or scheduling must have both.

## Profile Index

| Profile | Baseline role | Current runtime path | Build flavor | Define | Input plan | Evidence | Success anchors |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `schedule` | targeted mechanism sentinel | Ke scheduler/thread demo | `test-schedule` | `HO_DEMO_TEST_SCHEDULE` | none | host normally enough | `[DEMO] Selected profile: schedule`, scheduler/thread demo pass anchors |
| `kthread_pool_race` | targeted mechanism sentinel | Ke pool synchronization | `test-kthread_pool_race` | `HO_DEMO_TEST_KTHREAD_POOL_RACE` | none | host normally enough | `[TEST] KTHREAD pool race regression suite passed` |
| `user_hello` | formal ABI smoke profile | `libsys.h` write + clean exit payload | `test-user_hello` | `HO_DEMO_TEST_USER_HELLO` | none | host normally enough | `[USERRT] enter user mode`, `[USERRT] invalid user buffer`, `[USERHELLO] hello`, `[USERRT] SYS_EXIT`, `[USERRT] runtime teardown complete` |
| `user_caps` | formal capability/wait regression | `libsys.h` capability seed + handle syscalls + clean exit | `test-user_caps` | `HO_DEMO_TEST_USER_CAPS` | none | host normally enough | `[USERCAP] stdout capability write succeeds`, `[USERCAP] SYS_CLOSE succeeded`, `[USERCAP] capability syscall rejected`, `[USERCAP] SYS_WAIT_ONE timed out`, `[USERRT] SYS_EXIT` |
| `user_dual` | official contract (timing-sensitive) | `ExSpawnProgram()` / `ExWaitProcess()` compiled-userspace path | `test-user_dual` | `HO_DEMO_TEST_USER_DUAL` | none | host and TCG required | formal-ABI `user_hello`, direct-entry `user_counter`, `SYS_EXIT`, runtime teardown, no raw/P1 anchors, no teardown panic |
| `user_input` | official contract (timing-sensitive) | `ExSpawnProgram()` / `ExSetForegroundProcess()` / `ExWaitProcess()` foreground path | `test-user_input` | `HO_DEMO_TEST_USER_INPUT` | `scripts/input_plans/user_input.plan` | host and TCG required | `[USERINPUT] foreground -> hsh`, `[HSH] hello`, `[HSH] handoff`, `[USERINPUT] foreground -> calc`, `[CALC] 3 4 +`, clean teardown |
| `demo_shell` | official contract (timing-sensitive) | `ExSpawnProgram()` / `ExWaitProcess()` shell path; `ps` formats `EX_SYSINFO_CLASS_PROCESS_LIST` in user space | `test-demo_shell` | `HO_DEMO_TEST_DEMO_SHELL` | `scripts/input_plans/demo_shell.plan` | host and TCG required | `HimuOS System Information`, `HimuOS Virtual Memory Map`, `SYS_QUERY_SYSINFO succeeded class=6`, `PID  STATE`, `[CALC] result=7`, `[HSH] killed pid=`, `[HSH] HSH exited` |
| `user_fault` | official contract (timing-sensitive) | `demo_shell` control plane + user-fault recovery; `ps` formats `EX_SYSINFO_CLASS_PROCESS_LIST` in user space | `test-user_fault` | `HO_DEMO_TEST_USER_FAULT` | `scripts/input_plans/user_fault.plan` | host and TCG required | `[USERFAULT] #DE`, `[USERFAULT] #PF`, `[USERFAULT] CR2=`, `SYS_QUERY_SYSINFO succeeded class=6`, `[DEMOSHELL] foreground restored`, `[HSH] HSH exited` |
| `guard_wait` | targeted mechanism sentinel | guard misuse panic | `test-guard_wait` | `HO_DEMO_TEST_GUARD_WAIT` | none | targeted panic evidence | `[GUARDWAIT-`, diagnosable guard violation |
| `owned_exit` | targeted mechanism sentinel | owned-exit panic | `test-owned_exit` | `HO_DEMO_TEST_OWNED_EXIT` | none | targeted panic evidence | `[OWNEDEXIT-`, diagnosable owned-exit violation |
| `irql_wait` | targeted mechanism sentinel | DISPATCH_LEVEL wait panic | `test-irql_wait` | `HO_DEMO_TEST_IRQL_WAIT` | none | targeted panic evidence | `[IRQLWAIT-`, diagnosable DISPATCH_LEVEL wait violation |
| `irql_sleep` | targeted mechanism sentinel | DISPATCH_LEVEL sleep panic | `test-irql_sleep` | `HO_DEMO_TEST_IRQL_SLEEP` | none | targeted panic evidence | `[IRQLSLEEP-`, diagnosable DISPATCH_LEVEL sleep violation |
| `irql_yield` | targeted mechanism sentinel | DISPATCH_LEVEL yield panic | `test-irql_yield` | `HO_DEMO_TEST_IRQL_YIELD` | none | targeted panic evidence | `[IRQLYIELD-`, diagnosable DISPATCH_LEVEL yield violation |
| `irql_exit` | targeted mechanism sentinel | DISPATCH_LEVEL exit panic | `test-irql_exit` | `HO_DEMO_TEST_IRQL_EXIT` | none | targeted panic evidence | `[IRQLEXIT-`, diagnosable DISPATCH_LEVEL exit violation |
| `pf_imported` | targeted mechanism sentinel | imported-region NX fault | `test-pf_imported` | `HO_DEMO_TEST_PF_IMPORTED` | none | targeted page-fault evidence | `[PF-DEMO] triggering NX execute fault`, page-fault diagnostic output |
| `pf_guard` | targeted mechanism sentinel | stack guard fault | `test-pf_guard` | `HO_DEMO_TEST_PF_GUARD` | none | targeted page-fault evidence | `[PF-DEMO] triggering guard fault`, page-fault diagnostic output |
| `pf_fixmap` | targeted mechanism sentinel | fixmap NX fault | `test-pf_fixmap` | `HO_DEMO_TEST_PF_FIXMAP` | none | targeted page-fault evidence | fixmap NX page-fault diagnostic output |
| `pf_heap` | targeted mechanism sentinel | heap-backed KVA NX fault | `test-pf_heap` | `HO_DEMO_TEST_PF_HEAP` | none | targeted page-fault evidence | heap-backed KVA page-fault diagnostic output |

## Canonical Phase A Commands

### Build Sanity

```bash
make clean
bear -- make all
make test list
```

### user_dual

```bash
make clean
bear -- make all BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL
BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL \
    QEMU_CAPTURE_MODE=host bash scripts/qemu_capture.sh 30 /tmp/himuos-user-dual-host.log
BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL \
    QEMU_CAPTURE_MODE=tcg bash scripts/qemu_capture.sh 30 /tmp/himuos-user-dual-tcg.log
```

### user_input

```bash
make clean
bear -- make all BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT
BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT \
    QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/user_input.plan \
    bash scripts/qemu_capture.sh 20 /tmp/himuos-user-input-host.log
BUILD_FLAVOR=test-user_input HO_DEMO_TEST_NAME=user_input HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_INPUT \
    QEMU_CAPTURE_MODE=tcg QEMU_SENDKEY_PLAN=scripts/input_plans/user_input.plan \
    bash scripts/qemu_capture.sh 20 /tmp/himuos-user-input-tcg.log
```

### demo_shell

```bash
make clean
bear -- make all BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL
BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL \
    QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/demo_shell.plan \
    QEMU_CAPTURE_EXIT_ON='[HSH] HSH exited' \
    bash scripts/qemu_capture.sh 25 /tmp/himuos-demo-shell-host.log
BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL \
    QEMU_CAPTURE_MODE=tcg QEMU_SENDKEY_PLAN=scripts/input_plans/demo_shell.plan \
    QEMU_CAPTURE_EXIT_ON='[HSH] HSH exited' \
    bash scripts/qemu_capture.sh 25 /tmp/himuos-demo-shell-tcg.log
```

### user_fault

```bash
make clean
bear -- make all BUILD_FLAVOR=test-user_fault HO_DEMO_TEST_NAME=user_fault HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_FAULT
BUILD_FLAVOR=test-user_fault HO_DEMO_TEST_NAME=user_fault HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_FAULT \
    QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/user_fault.plan \
    QEMU_CAPTURE_EXIT_ON='[HSH] HSH exited' \
    bash scripts/qemu_capture.sh 25 /tmp/himuos-user-fault-host.log
BUILD_FLAVOR=test-user_fault HO_DEMO_TEST_NAME=user_fault HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_FAULT \
    QEMU_CAPTURE_MODE=tcg QEMU_SENDKEY_PLAN=scripts/input_plans/user_fault.plan \
    QEMU_CAPTURE_EXIT_ON='[HSH] HSH exited' \
    bash scripts/qemu_capture.sh 25 /tmp/himuos-user-fault-tcg.log
```

## Evidence Recording

For normal clean passes, record the command, log path, QEMU mode, and matched
anchors in the PR or change notes. Raw logs do not need to be committed unless
they document a bug or a hard-to-reproduce timing issue.

No-exit-on captures such as `user_dual` and `user_input` may end through the
watchdog after the expected anchors have appeared. Treat those runs by anchor
evidence, not by the capture wrapper status alone.

For failures, keep the log path and record:

- profile, mode, and command
- last matched anchor
- first failure or panic anchor
- whether the failure appears host-only, TCG-only, or common

## Phase C New-Era Clean Evidence 2026-05-02

Build and capture sanity after deleting raw/P1 support and moving `user_caps`
onto the formal ABI:

- `make all`: passed.
- `make all BUILD_FLAVOR=test-user_caps HO_DEMO_TEST_NAME=user_caps HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_CAPS`:
  passed.

Formal capability/wait evidence:

| Profile | Mode | Log | Result |
| --- | --- | --- | --- |
| `user_caps` | host | `/tmp/himuos-user-caps-host.log` | Matched capability write, close, stale-handle rejection, wait timeout, `SYS_EXIT`, runtime teardown, and reaper anchors; no raw/P1, panic, or STOP anchors. |

## Phase B New-Era Clean Evidence 2026-05-02

Build and capture sanity after moving `user_hello` onto the formal ABI:

- `make all BUILD_FLAVOR=test-user_dual HO_DEMO_TEST_NAME=user_dual HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_DUAL`:
  passed.

Timing-sensitive evidence:

| Profile | Mode | Log | Result |
| --- | --- | --- | --- |
| `user_dual` | host | `/tmp/himuos-user-dual-host.log` | Matched formal-ABI `[USERHELLO] hello`, `user_counter`, `SYS_EXIT`, runtime teardown, and reaper anchors; no `SYS_RAW_EXIT`, P1 gate, timer-gate, panic, or STOP anchors. |
| `user_dual` | TCG | `/tmp/himuos-user-dual-tcg.log` | Matched the same anchors as host; no `SYS_RAW_EXIT`, P1 gate, timer-gate, panic, or STOP anchors. |

## Historical New-Era Audit Evidence 2026-05-01

Build and list sanity after retiring the Bootstrap compatibility surfaces and
scoping raw/P1 behavior to sentinels:

- `make all`: passed for the default build.
- `make test TEST_MODULE=list`: passed and listed the expected profile set.
- `git diff --check`: passed.

Raw/P1 sentinel evidence:

| Profile | Mode | Log | Result |
| --- | --- | --- | --- |
| `user_hello` | host | `/tmp/himuos-user-hello-host.log` | Matched P1 gate, rejected guard-page raw write, `SYS_RAW_EXIT`, runtime teardown, and reaper anchors; no panic/STOP anchors. |
| `user_caps` | host | `/tmp/himuos-user-caps-host.log` | Matched capability write/close/stale-handle/wait-timeout anchors plus `SYS_RAW_EXIT` and runtime teardown; no panic/STOP anchors. |

Timing-sensitive final acceptance evidence:

| Profile | Mode | Log | Result |
| --- | --- | --- | --- |
| `demo_shell` | host | `/tmp/himuos-demo-shell-host.log` | Matched sysinfo, memmap, `ps`, foreground `calc`, kill, and `[HSH] HSH exited`; no panic/STOP anchors. |
| `demo_shell` | TCG | `/tmp/himuos-demo-shell-tcg.log` | Matched the same anchors as host; no panic/STOP anchors. |
| `user_input` | host | `/tmp/himuos-user-input-host.log` | Matched foreground handoff to `hsh`, `hsh` echo/handoff, foreground handoff to `calc`, `[CALC] 3 4 +`, teardown, and foreground owner reset anchors; no panic/STOP anchors. |
| `user_input` | TCG | `/tmp/himuos-user-input-tcg.log` | Matched the same anchors as host; no panic/STOP anchors. |
| `user_dual` | host | `/tmp/himuos-user-dual-host.log` | Matched concurrent `user_hello` and `user_counter` entry/output, runtime teardown, and profile termination anchors; no panic/STOP anchors. |
| `user_dual` | TCG | `/tmp/himuos-user-dual-tcg.log` | Matched the same anchors as host; no panic/STOP anchors. |
| `user_fault` | host | `/tmp/himuos-user-fault-host.log` | Matched user-mode `#DE`, user-mode `#PF`, `CR2`, foreground restore, `ps`, and `[HSH] HSH exited`; no panic/STOP anchors. |
| `user_fault` | TCG | `/tmp/himuos-user-fault-tcg.log` | Matched the same anchors as host; no panic/STOP anchors. |

## Phase D Runtime Table Evidence 2026-04-30

Build and list sanity:

- `make all`: passed for the default build.
- `make test TEST_MODULE=list`: passed and listed the expected profile set.

Timing-sensitive profile evidence after replacing the runtime alias registry
and process-control child table with Ex runtime process/thread tables:

| Profile | Mode | Log | Result |
| --- | --- | --- | --- |
| `demo_shell` | host | `/tmp/himuos-demo-shell-host.log` | Matched sysinfo, memmap, `ps`, foreground `calc`, kill, and `[HSH] HSH exited`; no panic/STOP anchors. |
| `demo_shell` | TCG | `/tmp/himuos-demo-shell-tcg.log` | Matched the same anchors as host; no panic/STOP anchors. |
| `user_fault` | host | `/tmp/himuos-user-fault-host.log` | Matched user-mode `#DE`, user-mode `#PF`, `CR2`, foreground restore, wait completion, `ps`, and `[HSH] HSH exited`; no panic/STOP anchors. |
| `user_fault` | TCG | `/tmp/himuos-user-fault-tcg.log` | Matched the same anchors as host; no panic/STOP anchors. |
| `user_dual` | host | `/tmp/himuos-user-dual-host.log` | Historical pre-Phase-B evidence matched user-mode entry, sentinel P1 gate, `user_hello`, direct-entry `user_counter`, `SYS_RAW_EXIT`, `SYS_EXIT`, teardown, and reaper anchors; capture ended by watchdog after anchors. |
| `user_dual` | TCG | `/tmp/himuos-user-dual-tcg.log` | Matched the same anchors as host; capture ended by watchdog after anchors. |
| `user_input` | host | `/tmp/himuos-user-input-host.log` | Matched foreground handoff to `hsh`, `hsh` echo/handoff, foreground handoff to `calc`, `[CALC] 3 4 +`, teardown, and foreground owner reset anchors; capture ended by watchdog after anchors. |
| `user_input` | TCG | `/tmp/himuos-user-input-tcg.log` | Matched the same anchors as host; capture ended by watchdog after anchors. |

## Phase C Hook Rename Evidence 2026-04-30

Build and list sanity:

- `make clean`: passed.
- `bear -- make all`: passed for the default build.
- `make test list`: passed and listed the expected profile set.

Timing-sensitive profile evidence after renaming the Ke/Ex runtime hook
contract:

| Profile | Mode | Log | Result |
| --- | --- | --- | --- |
| `demo_shell` | host | `/tmp/himuos-demo-shell-host.log` | Matched sysinfo, memmap, `ps`, foreground `calc`, kill, and `[HSH] HSH exited`; capture exit watcher returned success. |
| `demo_shell` | TCG | `/tmp/himuos-demo-shell-tcg.log` | Matched the same anchors as host; capture exit watcher returned success. |
| `user_fault` | host | `/tmp/himuos-user-fault-host.log` | Matched user-mode `#DE`, user-mode `#PF`, `CR2`, foreground restore, wait completion, `ps`, and `[HSH] HSH exited`; capture exit watcher returned success. |
| `user_fault` | TCG | `/tmp/himuos-user-fault-tcg.log` | Matched the same anchors as host; capture exit watcher returned success. |
| `user_dual` | host | `/tmp/himuos-user-dual-host.log` | Matched user-mode entry, timer gate, `user_hello`, `user_counter`, `SYS_RAW_EXIT`, `SYS_EXIT`, teardown, and reaper anchors; capture ended by watchdog after anchors. |
| `user_dual` | TCG | `/tmp/himuos-user-dual-tcg.log` | Matched the same anchors as host; capture ended by watchdog after anchors. |
| `user_input` | host | `/tmp/himuos-user-input-host.log` | Matched foreground handoff to `hsh`, `hsh` echo/handoff, foreground handoff to `calc`, `[CALC] 3 4 +`, and teardown anchors; capture ended by watchdog after anchors. |
| `user_input` | TCG | `/tmp/himuos-user-input-tcg.log` | Matched the same anchors as host; capture ended by watchdog after anchors. |

## Phase A Baseline Evidence 2026-04-28

Build and list sanity:

- `make clean`: passed.
- `bear -- make all`: passed for the default build.
- `make test list`: passed and listed the expected profile set.

Timing-sensitive profile evidence:

| Profile | Mode | Log | Result |
| --- | --- | --- | --- |
| `user_dual` | host | `/tmp/himuos-user-dual-host.log` | Matched user-mode entry, `user_hello`, `user_counter`, `SYS_RAW_EXIT`, `SYS_EXIT`, teardown, and reaper anchors; capture ended by watchdog after anchors. |
| `user_dual` | TCG | `/tmp/himuos-user-dual-tcg.log` | Matched the same anchors as host; capture ended by watchdog after anchors. |
| `user_input` | host | `/tmp/himuos-user-input-host.log` | Matched foreground handoff to `hsh`, `hsh` echo/handoff, foreground handoff to `calc`, `[CALC] 3 4 +`, and teardown anchors; capture ended by watchdog after anchors. |
| `user_input` | TCG | `/tmp/himuos-user-input-tcg.log` | Matched the same anchors as host; capture ended by watchdog after anchors. |
| `demo_shell` | host | `/tmp/himuos-demo-shell-host.log` | Matched sysinfo, memmap, ps, background `tick1s`, foreground `calc`, kill, and `[HSH] HSH exited`; capture exit watcher returned success. |
| `demo_shell` | TCG | `/tmp/himuos-demo-shell-tcg.log` | Matched the same anchors as host; capture exit watcher returned success. |
| `user_fault` | host | `/tmp/himuos-user-fault-host.log` | Matched user-mode `#DE`, user-mode `#PF`, `CR2`, foreground restore, wait completion, `ps`, and `[HSH] HSH exited`; capture exit watcher returned success. |
| `user_fault` | TCG | `/tmp/himuos-user-fault-tcg.log` | Matched the same anchors as host; capture exit watcher returned success. |

Harness note:

- `scripts/input_plans/user_fault.plan` was adjusted during the baseline freeze to avoid a
  log-order race between `calc>` and `SYS_SPAWN_PROGRAM`; the plan now waits on
  stable behavior anchors instead of capturing the child PID from an
  order-sensitive log line.
