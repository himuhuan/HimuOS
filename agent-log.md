# Agent Log — MVP P1: Introduce Minimal Userspace Toolchain

**Change:** `introduce-minimal-userspace-toolchain`  
**Branch:** `feature/introduce-compiled-bootstrap-user-programs`  
**Commits:** `8619ad1` → `519fb96` → `5b54918`  
**Date:** 2026-04-05

---

## Scope and Result

MVP P1 replaces the previously hand-assembled `user_hello` bytecodes with a real compiled userspace program. `src/user/user_hello/main.c` is now the authoritative source; the kernel consumes flat binary artifacts (`user_hello.code.bin`, `user_hello.const.bin`) — generated from source during the build — linked into the kernel image. The `ExBootstrapCreateProcess()` `CodeBytes`/`ConstBytes` launch contract is unchanged.

---

## Implementation — Phase Summary

| Phase | Commit | Change |
|---|---|---|
| 1 — Toolchain scaffold | `8619ad1` | Added `src/user/crt0.S` (syscall `int 0x80` entry/exit), `src/user/libsys.h` (inline wrappers for `SYS_RAW_WRITE`, `SYS_RAW_EXIT`, `HoUserWaitForP1Gate`, `HoUserAbort`), `src/user/user.ld` (`.text` at `0x80000000`, `.rodata` at const-page slot), and `src/user/user_hello/main.c` (P1 evidence chain: wait gate → probe illegal write → write hello → exit). Makefile gained `USER_CFLAGS`, `USER_LDFLAGS`, `USR_BUILDROOT` directories, and a `user` phony target producing `.elf`, `.code.bin`, and `.const.bin` via `objcopy`. `all` now depends on `efi kernel user`. |
| 2 — Kernel wiring | `519fb96` | Added `src/kernel/demo/user_hello_artifact_bridge.c`, which exports `KiUserHelloGetEmbeddedArtifacts()`. The bridge resolves four `extern` boundary symbols (`gKiUserHelloCode/ConstBytesStart/End`) exposed by the Makefile embedding pipeline: `ld -r -b binary` turns each `.bin` into a relocatable object, then `objcopy --rename-section` and `--redefine-sym` rename the data section and rewrite the auto-generated linker symbols to the canonical `gKiUserHello{Code,Const}Bytes{Start,End}` names. The bridge computes lengths with an underflow guard and panics on empty artifacts. `demo_internal.h` gained `KI_USER_HELLO_EMBEDDED_ARTIFACTS`. `user_hello.c` was stripped of its old inline bytecode and now calls the bridge. The two embedded artifact objects (`user_hello.code.bin.o`, `user_hello.const.bin.o`) are appended to `OBJS_KERNEL` unconditionally; `BUILD_FLAVOR` only redirects output directories. |
| 3 — Documentation | `5b54918` | `Readme.md` updated to show the `BUILD_FLAVOR=test-user_hello` invocation. `user_hello.c` inline comment corrected to name `src/user/user_hello` as the source. Makefile comment wording cleaned up. |

---

## Review Notes

- **Phase 1** — No blocking issues. Non-blocking: the `test` / `run` path did not yet force the generated user artifacts into the kernel-side path because phase 1 stopped at standalone artifact generation. Resolved by phase 2.
- **Phase 2** — No blocking issues. Non-blocking: `KiUserHelloGetEmbeddedArtifacts()` panics if `ConstLength == 0`, which is acceptable given the current `src/user/user_hello/main.c` payload always carries rodata; noted as a known assumption.
- **Phase 3** — No blocking issues. One wording pass after review: docs and inline comment now explicitly state the path is built from `src/user/user_hello` sources.

---

## Validation

Build and capture sequence:

```sh
make clean && bear -- make all BUILD_FLAVOR=test-user_hello \
    HO_DEMO_TEST_NAME=user_hello HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO

BUILD_FLAVOR=test-user_hello HO_DEMO_TEST_NAME=user_hello \
    HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_USER_HELLO \
    bash scripts/qemu_capture.sh 30 /tmp/himuos-user-hello-final.log
```

QEMU capture confirmed these key anchors in order:

1. `enter user mode`
2. `P1 gate armed`
3. `invalid raw write rejected`
4. `hello write succeeds`
5. `SYS_RAW_EXIT`
6. `bootstrap teardown complete`
7. `idle/reaper reclaimed`

The captured log also retained the intermediate P1 timer round-trip and thread-termination evidence. No panics or unexpected halts were observed.

---

## Final Repository State

- **New files:** `src/user/crt0.S`, `src/user/libsys.h`, `src/user/user.ld`, `src/user/user_hello/main.c`, `src/kernel/demo/user_hello_artifact_bridge.c`
- **Modified:** `makefile` (toolchain rules, `ld -r -b binary` / `objcopy` artifact embedding, `user` target), `src/kernel/demo/demo_internal.h` (`KI_USER_HELLO_EMBEDDED_ARTIFACTS`, `KiUserHelloGetEmbeddedArtifacts` decl), `src/kernel/demo/user_hello.c` (removed inline bytecodes, calls bridge), `Readme.md`
- **Kernel launch contract:** unchanged — `ExBootstrapCreateProcess()` still takes raw `CodeBytes`/`ConstBytes`; artifact content is now compiler-generated rather than hand-written
- **OpenSpec change files:** not committed (intentional, per project convention)
