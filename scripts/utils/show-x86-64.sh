#!/usr/bin/env python3

import re
import subprocess
import sys
from pathlib import Path

ARCH_DIR = "x86-64"


def format_hex(value: int) -> str:
    return f"0X{value:X}"


def parse_address(text: str) -> int:
    value = text.strip()
    base = 16 if value.lower().startswith("0x") else 10
    return int(value, base)


def load_symbols(map_path: Path):
    symbol_pattern = re.compile(r"^\s*(0x[0-9a-fA-F]+)\s+([A-Za-z_][A-Za-z0-9_]*)")
    bss_pattern = re.compile(r"^\s*\.bss\s+(0x[0-9a-fA-F]+)")
    symbols = []
    bss_raw = None
    bss_value = None

    with map_path.open("r", encoding="ascii", errors="ignore") as handle:
        for line in handle:
            match = symbol_pattern.match(line)
            if match:
                addr_str = match.group(1)
                name = match.group(2)
                addr_value = int(addr_str, 16)
                symbols.append((addr_value, name, addr_str))
                continue

            if bss_raw is None:
                bss_match = bss_pattern.match(line)
                if bss_match:
                    bss_raw = bss_match.group(1)
                    bss_value = int(bss_raw, 16)

    return symbols, bss_raw, bss_value


def find_symbol(symbols, target_addr: int):
    best_index = None
    for index, (addr, _, _) in enumerate(symbols):
        if addr <= target_addr:
            best_index = index
        else:
            break

    if best_index is None:
        return None, None

    current = symbols[best_index]

    next_addr = None
    next_raw = None
    for addr, _, raw in symbols[best_index + 1:]:
        if addr > current[0]:
            next_addr = addr
            next_raw = raw
            break

    return current, (next_addr, next_raw)


def highlight_disassembly(text: str, target_addr: int, context: int):
    addr_regex = re.compile(r"^\s*([0-9a-fA-F]+):")
    lines = text.splitlines()
    target_index = None

    for index, line in enumerate(lines):
        match = addr_regex.match(line)
        if match and int(match.group(1), 16) == target_addr:
            target_index = index
            break

    if target_index is None:
        for line in lines:
            print(line)
        return

    start = max(0, target_index - context)
    end = min(len(lines), target_index + context + 1)

    for index in range(start, end):
        if index == target_index:
            print(f">>> {lines[index]} <<<")
        else:
            print(lines[index])


def run_command(command):
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        if result.stderr:
            sys.stderr.write(result.stderr)
        raise RuntimeError(f"Command failed: {' '.join(command)}")
    return result.stdout


def main() -> int:
    if len(sys.argv) < 2 or len(sys.argv) > 3:
        print("Usage: show-x86-64.sh <address> [context_lines] (hex with 0x or decimal)")
        print("  context_lines: number of lines to show before and after target (default: 20)")
        return 1

    try:
        target_addr = parse_address(sys.argv[1])
    except ValueError:
        print("Invalid address.")
        return 1

    context_lines = 20
    if len(sys.argv) == 3:
        try:
            context_lines = int(sys.argv[2])
            if context_lines < 0:
                raise ValueError
        except ValueError:
            print("context_lines must be a non-negative integer.")
            return 1

    script_dir = Path(__file__).resolve().parent
    root_dir = script_dir.parent.parent
    map_path = root_dir / "build" / ARCH_DIR / "kernel" / "exos.map"
    elf_path = root_dir / "build" / ARCH_DIR / "kernel" / "exos.elf"

    if not map_path.is_file():
        print(f"Missing map file: {map_path}")
        print(f"Build the {ARCH_DIR} kernel before using this tool.")
        return 1

    if not elf_path.is_file():
        print(f"Missing ELF file: {elf_path}")
        print(f"Build the {ARCH_DIR} kernel before using this tool.")
        return 1

    print(f"Input address: {format_hex(target_addr)}")

    symbols, bss_raw, bss_value = load_symbols(map_path)
    if not symbols:
        print("No symbol found.")
        return 1

    current, next_symbol = find_symbol(symbols, target_addr)
    if current is None:
        print("No symbol found.")
        return 1

    func_addr, func_name, func_raw = current
    print(f"Symbol found: {func_name} ({format_hex(func_addr)})")

    next_addr = next_raw = None
    if next_symbol is not None:
        next_addr, next_raw = next_symbol

    if bss_value is not None and func_addr >= bss_value:
        print(f"Symbol {func_name} ({format_hex(func_addr)}) is in BSS section or beyond - no disassembly/dump performed")
        print(f"BSS section starts at: {bss_raw}")
        return 0

    in_range = False
    if next_addr is not None:
        in_range = func_addr <= target_addr < next_addr
    else:
        in_range = target_addr >= func_addr

    if in_range:
        print(f"Within function {func_name} ({format_hex(func_addr)})")
        if next_addr is not None:
            print(f"Disassembly from {format_hex(func_addr)} to {format_hex(next_addr)}")
            command = [
                "objdump",
                "-d",
                str(elf_path),
                f"--start-address={hex(func_addr)}",
                f"--stop-address={hex(next_addr)}",
            ]
        else:
            print(f"Disassembly from {format_hex(func_addr)} (unknown size)")
            command = [
                "objdump",
                "-d",
                str(elf_path),
                f"--start-address={hex(func_addr)}",
            ]

        try:
            output = run_command(command)
        except RuntimeError:
            return 1

        highlight_disassembly(output, target_addr, context_lines)
    else:
        print(f"No function found, closest symbol below: {func_name} ({format_hex(func_addr)})")
        command = [
            "hexdump",
            "-C",
            "-n",
            "256",
            "-s",
            str(func_addr),
            str(elf_path),
        ]
        try:
            output = run_command(command)
        except RuntimeError:
            return 1
        print(output, end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
