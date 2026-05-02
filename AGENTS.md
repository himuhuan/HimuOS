# Repository Guidelines

## Project Structure & Module Organization

HimuOS is a UEFI x86_64 macro-kernel project. Source lives under `src/`: `src/boot/v2` is the UEFI loader path, `src/kernel` contains initialization, Ke/Ex subsystems, demos, and MM/thread/time code, `src/arch/amd64` holds architecture-specific C and NASM, and `src/drivers` contains device drivers. Public headers are in `src/include`. User-mode demos live in `src/user/<program>` with shared support in `src/user/libsys.h` and `src/user/user.ld`. Shared libraries are in `src/lib` and `src/libc`; fonts and other assets are in `src/assets`. Use `docs/` for API notes, diagrams, and fix reports, and `scripts/` for QEMU automation and input plans.

## Build, Test, and Development Commands

- `make all`: builds the EFI loader, kernel, and user artifacts.
- `make run`: copies the default interactive `demo_shell` flavor into `esp/` and boots QEMU with OVMF.
- `make debug`: boots QEMU with a GDB server using `-s -S`.
- `make iso` / `make run_iso`: creates and boots `himu_os.iso`.
- `make clean`: removes build output, copied ESP artifacts, and ISO staging files.
- `make clean_code`: runs `clang-format` over `src/**/*.c` and `src/**/*.h`.
- `make test TEST_MODULE=list`: lists regression profiles and recommended commands.

Preferred regression flow:

```bash
make clean
bear -- make all BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL
BUILD_FLAVOR=test-demo_shell HO_DEMO_TEST_NAME=demo_shell HO_DEMO_TEST_DEFINE=HO_DEMO_TEST_DEMO_SHELL \
    QEMU_CAPTURE_MODE=host QEMU_SENDKEY_PLAN=scripts/input_plans/demo_shell.plan \
    bash scripts/qemu_capture.sh 25 /tmp/himuos-demo-shell-host.log
```

For timing-sensitive profiles such as `user_dual`, `user_input`, `demo_shell`, and `user_fault`, collect both `QEMU_CAPTURE_MODE=host` and `QEMU_CAPTURE_MODE=tcg` logs.

## Coding Style & Naming Conventions

C code follows `.clang-format`: Microsoft base style, 4 spaces, no tabs, 120-column limit, preserved include order, and unsorted includes. Keep existing prefixes (`Ke*`, `Ki*`, `Ex*`, `Ho*`) and subsystem-local naming. Use `.c`/`.h` for C, `.asm` for NASM kernel assembly, and `.S` for user assembly stubs. Keep comments short and useful around ABI, paging, interrupts, and scheduling.

## Testing Guidelines

There is no standalone unit-test tree; regression coverage is profile-driven through kernel demos and QEMU serial logs. Add or update profiles in `src/kernel/demo` when changing scheduler, memory, syscall, input, or user-process behavior. Name profiles as `HO_DEMO_TEST_<NAME>` and use matching `BUILD_FLAVOR=test-<name>` values. For timing-sensitive fixes, preserve logs or summarize pass/fail evidence in `docs/`.

## Commit & Pull Request Guidelines

Recent history uses concise imperative subjects, often with prefixes such as `feat:` and `build:`. Keep subjects focused, for example `feat: add demo shell sysinfo command`. Pull requests should describe the affected subsystem, list exact build/regression commands, link issues or fix reports, and include screenshots or serial-log excerpts for console-visible changes.
