#!/usr/bin/env python3
"""
Send a simple scripted key plan to a QEMU HMP unix monitor socket.

Plan format:
  - Blank lines and lines starting with '#' are ignored.
  - 'wait_for <substring>' waits until the substring appears in the capture log.
  - 'capture <name> <regex>' waits until the regex matches the capture log and
    stores the first capture group (or full match) into <name>.
  - 'wait <seconds>' sleeps for the given duration.
  - 'text <literal text>' types the given text character-by-character.
  - 'key <hmp-key>' sends one raw HMP sendkey token.

The 'wait_for', 'capture', 'text', and 'key' payloads may reference captured
variables using '{{name}}'.

Only a bounded ASCII subset is supported because the P1 profile only needs
letters, digits, spaces and a few punctuation keys.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import socket
import sys
import time


SHIFT_MAP = {
    "!": "shift-1",
    "@": "shift-2",
    "#": "shift-3",
    "$": "shift-4",
    "%": "shift-5",
    "^": "shift-6",
    "&": "shift-7",
    "*": "shift-8",
    "(": "shift-9",
    ")": "shift-0",
    "_": "shift-minus",
    "+": "shift-equal",
    "{": "shift-leftbracket",
    "}": "shift-rightbracket",
    "|": "shift-backslash",
    ":": "shift-semicolon",
    '"': "shift-apostrophe",
    "<": "shift-comma",
    ">": "shift-dot",
    "?": "shift-slash",
    "~": "shift-grave_accent",
}

DIRECT_MAP = {
    " ": "spc",
    "-": "minus",
    "=": "equal",
    "[": "leftbracket",
    "]": "rightbracket",
    "\\": "backslash",
    ";": "semicolon",
    "'": "apostrophe",
    ",": "comma",
    ".": "dot",
    "/": "slash",
    "`": "grave_accent",
}


def char_to_hmp_key(char: str) -> str:
    if len(char) != 1:
        raise ValueError(f"expected single character, got {char!r}")

    if "a" <= char <= "z" or "0" <= char <= "9":
        return char

    if "A" <= char <= "Z":
        return f"shift-{char.lower()}"

    if char in DIRECT_MAP:
        return DIRECT_MAP[char]

    if char in SHIFT_MAP:
        return SHIFT_MAP[char]

    raise ValueError(f"unsupported plan character: {char!r}")


def read_until_prompt(connection: socket.socket, timeout: float = 5.0) -> str:
    connection.settimeout(timeout)
    chunks: list[bytes] = []

    while True:
        chunk = connection.recv(4096)
        if not chunk:
            break
        chunks.append(chunk)
        if b"(qemu)" in chunk:
            break

    return b"".join(chunks).decode("utf-8", "ignore")


def send_hmp_command(connection: socket.socket, command: str) -> None:
    connection.sendall((command + "\n").encode("utf-8"))
    read_until_prompt(connection)


def connect_monitor(path: pathlib.Path, timeout_s: float) -> socket.socket:
    deadline = time.time() + timeout_s

    while time.time() < deadline:
        if not path.exists():
            time.sleep(0.1)
            continue

        connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            connection.connect(str(path))
            read_until_prompt(connection)
            return connection
        except OSError:
            connection.close()
            time.sleep(0.1)

    raise TimeoutError(f"monitor socket did not become ready: {path}")


def wait_for_log_contains(log_path: pathlib.Path, pattern: str, timeout_s: float, start_index: int) -> int:
    deadline = time.time() + timeout_s

    while time.time() < deadline:
        if log_path.exists():
            content = log_path.read_text(encoding="utf-8", errors="ignore")
            offset = content.find(pattern, start_index)
            if offset >= 0:
                return offset + len(pattern)

        time.sleep(0.1)

    raise TimeoutError(f"log pattern did not appear within {timeout_s:.1f}s: {pattern!r}")


def capture_log_variable(log_path: pathlib.Path, pattern: str, timeout_s: float, start_index: int) -> tuple[str, int]:
    deadline = time.time() + timeout_s
    regex = re.compile(pattern, re.MULTILINE)

    while time.time() < deadline:
        if log_path.exists():
            content = log_path.read_text(encoding="utf-8", errors="ignore")
            match = regex.search(content, start_index)
            if match is not None:
                if match.lastindex:
                    return match.group(1), match.end()
                return match.group(0), match.end()

        time.sleep(0.1)

    raise TimeoutError(f"log regex did not match within {timeout_s:.1f}s: {pattern!r}")


def substitute_variables(value: str, variables: dict[str, str]) -> str:
    def replace(match: re.Match[str]) -> str:
        name = match.group(1)
        if name not in variables:
            raise KeyError(f"plan variable is not set: {name}")
        return variables[name]

    return re.sub(r"\{\{([A-Za-z_][A-Za-z0-9_]*)\}\}", replace, value)


def run_plan(connection: socket.socket, log_path: pathlib.Path, plan_path: pathlib.Path, pattern_timeout_s: float) -> None:
    variables: dict[str, str] = {}
    log_cursor = 0

    for raw_line in plan_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue

        op, _, payload = line.partition(" ")
        if op == "wait_for":
            log_cursor = wait_for_log_contains(log_path,
                                              substitute_variables(payload, variables),
                                              pattern_timeout_s,
                                              log_cursor)
            continue

        if op == "capture":
            name, _, pattern = payload.partition(" ")
            if not name or not pattern:
                raise ValueError("capture requires a variable name and regex pattern")

            variables[name], log_cursor = capture_log_variable(log_path,
                                                               substitute_variables(pattern, variables),
                                                               pattern_timeout_s,
                                                               log_cursor)
            continue

        if op == "wait":
            time.sleep(float(payload))
            continue

        if op == "text":
            for char in substitute_variables(payload, variables):
                send_hmp_command(connection, f"sendkey {char_to_hmp_key(char)}")
            continue

        if op == "key":
            send_hmp_command(connection, f"sendkey {substitute_variables(payload, variables)}")
            continue

        raise ValueError(f"unsupported plan operation: {op}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", required=True, help="QEMU HMP unix monitor socket")
    parser.add_argument("--log", required=True, help="Capture log file that receives QEMU serial output")
    parser.add_argument("--plan", required=True, help="Path to sendkey plan file")
    parser.add_argument("--connect-timeout", type=float, default=10.0, help="Seconds to wait for the monitor socket")
    parser.add_argument(
        "--pattern-timeout",
        type=float,
        default=20.0,
        help="Seconds to wait for each wait_for pattern to appear in the capture log",
    )
    args = parser.parse_args()

    socket_path = pathlib.Path(args.socket)
    log_path = pathlib.Path(args.log)
    plan_path = pathlib.Path(args.plan)
    if not plan_path.is_file():
        raise FileNotFoundError(f"plan file not found: {plan_path}")

    connection = connect_monitor(socket_path, args.connect_timeout)
    try:
        run_plan(connection, log_path, plan_path, args.pattern_timeout)
    finally:
        connection.close()

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - simple CLI failure path
        print(f"qemu_sendkeys.py: {exc}", file=sys.stderr)
        raise SystemExit(1)
