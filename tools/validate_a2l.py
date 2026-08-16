#!/usr/bin/env python3
"""
validate_a2l.py - Verify that A2L addresses match the compiled ELF.

Parses docs/a2l/xcp-pico.a2l for MEASUREMENT (ECU_ADDRESS) and
CHARACTERISTIC (VALUE address) entries, cross-checks each symbol name
against arm-none-eabi-nm output from the built ELF, and reports any
mismatch, missing symbol, or stale address.

This exists because A2L addresses are RAM addresses assigned by the
linker. They can change on any rebuild (new variables added/removed,
struct layout changes, compiler version changes). A2L files are not
automatically kept in sync with the linker; ASAM's own documentation
notes this is normally handled by tools like CANape's A2L Editor, which
re-reads the linker map file and updates addresses by symbol name. This
script is the equivalent check for this project's manually-maintained
A2L, run before every commit that touches firmware or the A2L file.

Usage:
    python tools/validate_a2l.py firmware/build/xcp_pico.elf docs/a2l/xcp-pico.a2l
"""

import re
import subprocess
import sys


def get_elf_symbols(elf_path: str) -> dict:
    """Return {symbol_name: address_int} from arm-none-eabi-nm."""
    result = subprocess.run(
        f"arm-none-eabi-nm {elf_path}",
        capture_output=True, text=True, shell=True
    )
    if result.returncode != 0:
        print(f"ERROR: arm-none-eabi-nm failed:\n{result.stderr}")
        sys.exit(1)

    symbols = {}
    for line in result.stdout.splitlines():
        parts = line.strip().split()
        if len(parts) == 3:
            addr_str, sym_type, name = parts
            # Only care about data symbols (.data/.bss): B, D, T sometimes
            # used inconsistently across nm builds; accept any single-char
            # type and let address comparison be the real check.
            try:
                symbols[name] = int(addr_str, 16)
            except ValueError:
                continue
    return symbols


def parse_a2l(a2l_path: str) -> list:
    """
    Return [(name, address_int, kind), ...] for MEASUREMENT (ECU_ADDRESS)
    and CHARACTERISTIC (VALUE address) entries in the A2L.

    This is a lightweight line-oriented parser, not a full ASAP2 grammar
    parser. It is sufficient for this project's hand-written, consistently
    formatted A2L and is not intended to parse arbitrary third-party A2L
    files.
    """
    with open(a2l_path, "r", encoding="utf-8") as f:
        text = f.read()

    entries = []

    # MEASUREMENT blocks: name on /begin MEASUREMENT line,
    # address on the ECU_ADDRESS line within the same block.
    meas_pattern = re.compile(
        r"/begin MEASUREMENT\s+(\S+).*?ECU_ADDRESS\s+(0x[0-9A-Fa-f]+)",
        re.DOTALL
    )
    for m in meas_pattern.finditer(text):
        name, addr = m.group(1), int(m.group(2), 16)
        entries.append((name, addr, "MEASUREMENT"))

    # CHARACTERISTIC blocks: name on /begin CHARACTERISTIC line,
    # address is the line immediately after "VALUE".
    char_pattern = re.compile(
        r"/begin CHARACTERISTIC\s+(\S+).*?VALUE\s*\n\s*(0x[0-9A-Fa-f]+)",
        re.DOTALL
    )
    for m in char_pattern.finditer(text):
        name, addr = m.group(1), int(m.group(2), 16)
        entries.append((name, addr, "CHARACTERISTIC"))

    return entries


def main():
    if len(sys.argv) < 3:
        print(f"Usage: python {sys.argv[0]} <elf path> <a2l path>")
        sys.exit(1)

    elf_path = sys.argv[1]
    a2l_path = sys.argv[2]

    print(f"Reading ELF symbols from {elf_path} ...")
    elf_symbols = get_elf_symbols(elf_path)
    print(f"  {len(elf_symbols)} symbols found.\n")

    print(f"Parsing A2L entries from {a2l_path} ...")
    a2l_entries = parse_a2l(a2l_path)
    print(f"  {len(a2l_entries)} entries found.\n")

    if not a2l_entries:
        print("WARNING: no MEASUREMENT/CHARACTERISTIC entries parsed. "
              "Check the A2L formatting or the parser's regex patterns.")
        sys.exit(1)

    ok = True
    print(f"{'Name':<16} {'Kind':<16} {'A2L addr':<12} {'ELF addr':<12} Status")
    print("-" * 70)

    for name, a2l_addr, kind in a2l_entries:
        if name not in elf_symbols:
            print(f"{name:<16} {kind:<16} 0x{a2l_addr:08X}   {'MISSING':<12} "
                  f"FAIL - symbol not found in ELF")
            ok = False
            continue

        elf_addr = elf_symbols[name]
        if elf_addr != a2l_addr:
            print(f"{name:<16} {kind:<16} 0x{a2l_addr:08X}   0x{elf_addr:08X}   "
                  f"FAIL - address mismatch")
            ok = False
        else:
            print(f"{name:<16} {kind:<16} 0x{a2l_addr:08X}   0x{elf_addr:08X}   OK")

    print()
    if ok:
        print("All A2L addresses match the ELF. A2L is up to date.")
        sys.exit(0)
    else:
        print("MISMATCH DETECTED. Update the affected addresses in the A2L:")
        print(f"  arm-none-eabi-nm {elf_path} | Select-String <symbol_name>")
        sys.exit(1)


if __name__ == "__main__":
    main()