#!/usr/bin/env python3
"""
find_bsod_pos.py [RIP]

Given a RIP printed by HimuOS BSOD, map it back to the nearest symbol in
build/kernel/bin/kernel.bin. The script tries `nm` first (sorted, defined
symbols only) and falls back to `objdump -t`. It also derives the kernel
base from himuos.ld to compute file offset.

Usage:
  python find_bsod_pos.py 0xFFFF800000001234
  python find_bsod_pos.py --kernel build/kernel/bin/kernel.bin --rip 0xffff... --objdump llvm-objdump
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass
from typing import Iterable, List, Optional, Tuple


DEFAULT_KERNEL = "build/kernel/bin/kernel.bin"
DEFAULT_LD = "himuos.ld"


@dataclass
class Symbol:
    name: str
    addr: int
    size: Optional[int]
    source: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Locate symbol for HimuOS BSOD RIP")
    parser.add_argument("rip", help="RIP address (hex like 0xFFFF..., or decimal)")
    parser.add_argument("--kernel", default=DEFAULT_KERNEL, help="Kernel binary path")
    parser.add_argument("--ld", default=DEFAULT_LD, help="Linker script path (for KERNEL_BASE)")
    parser.add_argument("--nm", default="nm", help="nm tool path (used first)")
    parser.add_argument("--objdump", default="objdump", help="objdump tool path (fallback)")
    parser.add_argument("--around", type=int, default=0x40, help="Byte window for suggested disasm")
    return parser.parse_args()


def parse_int(value: str) -> int:
    value = value.strip().lower()
    base = 16 if value.startswith("0x") else 10
    return int(value, base)


def get_kernel_base(ld_path: pathlib.Path) -> Optional[int]:
    if not ld_path.exists():
        return None
    text = ld_path.read_text(encoding="utf-8", errors="ignore")
    match = re.search(r"KERNEL_BASE\s*=\s*(0x[0-9a-fA-F]+)", text)
    if match:
        return int(match.group(1), 16)
    return None


def run_cmd(cmd: List[str]) -> Tuple[int, str, str]:
    try:
        proc = subprocess.run(cmd, check=False, capture_output=True, text=True)
    except FileNotFoundError as exc:
        return 127, "", str(exc)
    return proc.returncode, proc.stdout, proc.stderr


def symbols_from_nm(nm_path: str, kernel: pathlib.Path) -> Iterable[Symbol]:
    cmd = [nm_path, "-n", "--print-size", "--defined-only", str(kernel)]
    code, out, err = run_cmd(cmd)
    if code != 0:
        debug = f"nm failed ({code}): {err.strip()}"
        print(debug, file=sys.stderr)
        return []

    for line in out.splitlines():
        parts = line.strip().split()
        # nm --print-size gives: ADDRESS SIZE TYPE NAME
        if len(parts) < 4:
            continue
        try:
            addr = int(parts[0], 16)
            size = int(parts[1], 16)
        except ValueError:
            continue
        name = parts[3]
        yield Symbol(name=name, addr=addr, size=size, source="nm")


def symbols_from_objdump(objdump_path: str, kernel: pathlib.Path) -> Iterable[Symbol]:
    cmd = [objdump_path, "-t", "-w", str(kernel)]
    code, out, err = run_cmd(cmd)
    if code != 0:
        debug = f"objdump failed ({code}): {err.strip()}"
        print(debug, file=sys.stderr)
        return []

    for line in out.splitlines():
        # Typical line: ffff800000000230 g     F .text  000000000000005d MyFunc
        parts = line.strip().split()
        if len(parts) < 6:
            continue
        addr_str, _, _, section, size_str, name = parts[:6]
        if section == "*UND*" or section == "*ABS*":
            continue
        if not all(c in "0123456789abcdefABCDEF" for c in addr_str):
            continue
        try:
            addr = int(addr_str, 16)
            size = int(size_str, 16) if all(c in "0123456789abcdefABCDEF" for c in size_str) else None
        except ValueError:
            continue
        yield Symbol(name=name, addr=addr, size=size, source="objdump")


def pick_symbol(rip: int, symbols: Iterable[Symbol]) -> Tuple[Optional[Symbol], Optional[Symbol]]:
    before: Optional[Symbol] = None
    after: Optional[Symbol] = None
    for sym in symbols:
        if sym.addr <= rip and (before is None or sym.addr > before.addr):
            before = sym
        if sym.addr > rip and (after is None or sym.addr < after.addr):
            after = sym
    return before, after


def format_hex(value: int) -> str:
    return f"0x{value:016x}"


def main() -> int:
    args = parse_args()
    rip = parse_int(args.rip)

    kernel_path = pathlib.Path(args.kernel)
    ld_path = pathlib.Path(args.ld)
    if not kernel_path.exists():
        print(f"Kernel binary not found: {kernel_path}", file=sys.stderr)
        return 1

    kbase = get_kernel_base(ld_path)
    offset = None
    if kbase is not None and rip >= kbase:
        offset = rip - kbase

    sym_list: List[Symbol] = list(symbols_from_nm(args.nm, kernel_path))
    if not sym_list:
        sym_list = list(symbols_from_objdump(args.objdump, kernel_path))

    before, after = pick_symbol(rip, sym_list)

    print("=== HimuOS BSOD RIP Locator ===")
    print(f"RIP          : {format_hex(rip)} ({rip})")
    if kbase is not None:
        print(f"KERNEL_BASE  : {format_hex(kbase)} (from {ld_path})")
        if offset is not None:
            print(f"File offset  : 0x{offset:x} bytes from kernel base")
    else:
        print("KERNEL_BASE  : <not found in linker script>")

    if before:
        delta = rip - before.addr
        size_str = f" size=0x{before.size:x}" if before.size else ""
        print(f"Symbol       : {before.name} @ {format_hex(before.addr)} (+0x{delta:x}){size_str} [{before.source}]")
    else:
        print("Symbol       : <not found>")

    if after:
        gap = after.addr - rip
        print(f"Next symbol  : {after.name} @ {format_hex(after.addr)} (gap 0x{gap:x}) [{after.source}]")

    if before is None and after is None:
        print("No symbols found; ensure nm/objdump is available.")

    start = rip - args.around
    stop = rip + args.around
    print("\nSuggested follow-ups:")
    print(f"  objdump -d -w --start-address={format_hex(max(start, 0))} --stop-address={format_hex(stop)} {kernel_path}")
    print(f"  addr2line -e {kernel_path} -a {format_hex(rip)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
