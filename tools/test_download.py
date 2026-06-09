#!/usr/bin/env python3
"""
test_download.py - Hardware acceptance tests for XCP DOWNLOAD command.

Run after flashing xcp_pico firmware. Tests write a float value to the
c_alpha variable, verify it was written, then restore the original value.

Usage:
    python tools/test_download.py COM15

Requires: pyserial
"""

import serial
import struct
import sys
import time
import subprocess
import re


# ---------------------------------------------------------------------------
# SxI / XCP helpers (same pattern as test_xcp_protocol.py)
# ---------------------------------------------------------------------------

class XcpClient:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 1.0):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.ctr = 0
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def _send(self, payload: bytes) -> bytes:
        header = struct.pack("<HH", len(payload), self.ctr)
        self.ser.write(header + payload)
        self.ctr += 1

        resp_header = self.ser.read(4)
        if len(resp_header) < 4:
            raise TimeoutError("No response from slave")
        resp_len, _ = struct.unpack("<HH", resp_header)
        resp_payload = self.ser.read(resp_len)
        if len(resp_payload) < resp_len:
            raise TimeoutError(f"Incomplete: {len(resp_payload)}/{resp_len}")
        return resp_payload

    def connect(self):
        resp = self._send(bytes([0xFF, 0x00]))
        assert resp[0] == 0xFF, f"CONNECT failed: 0x{resp[0]:02X}"

    def set_mta(self, addr: int):
        cmd = struct.pack("<BBBBI", 0xF6, 0x00, 0x00, 0x00, addr)
        resp = self._send(cmd)
        assert resp[0] == 0xFF, f"SET_MTA failed: 0x{resp[0]:02X}"

    def upload(self, num_bytes: int) -> bytes:
        cmd = bytes([0xF5, num_bytes])
        resp = self._send(cmd)
        assert resp[0] == 0xFF, f"UPLOAD failed: 0x{resp[0]:02X}"
        return resp[1:1 + num_bytes]

    def short_upload(self, addr: int, num_bytes: int) -> bytes:
        cmd = struct.pack("<BBBBI", 0xF4, num_bytes, 0x00, 0x00, addr)
        resp = self._send(cmd)
        assert resp[0] == 0xFF, f"SHORT_UPLOAD failed: 0x{resp[0]:02X}"
        return resp[1:1 + num_bytes]

    def download(self, data: bytes) -> None:
        cmd = bytes([0xF0, len(data)]) + data
        resp = self._send(cmd)
        assert resp[0] == 0xFF, f"DOWNLOAD failed: 0x{resp[0]:02X}"

    def download_expect_error(self, data: bytes) -> int:
        """Send DOWNLOAD and expect a negative response. Returns error code."""
        cmd = bytes([0xF0, len(data)]) + data
        resp = self._send(cmd)
        assert resp[0] == 0xFE, f"Expected error, got: 0x{resp[0]:02X}"
        return resp[1]


# ---------------------------------------------------------------------------
# Get symbol address from ELF
# ---------------------------------------------------------------------------

def get_symbol_addr(elf_path: str, symbol: str) -> int:
    result = subprocess.run(
    f"arm-none-eabi-nm {elf_path}",
    capture_output=True, text=True,
    shell=True
    )
    for line in result.stdout.splitlines():
        parts = line.strip().split()
        if len(parts) == 3 and parts[2] == symbol:
            return int(parts[0], 16)
    raise RuntimeError(f"Symbol '{symbol}' not found in {elf_path}")


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

PASS = "PASS"
FAIL = "FAIL"

def run_test(name: str, fn) -> bool:
    try:
        fn()
        print(f"  [{PASS}]  {name}")
        return True
    except AssertionError as e:
        print(f"  [{FAIL}]  {name}: {e}")
        return False
    except Exception as e:
        print(f"  [{FAIL}]  {name}: {type(e).__name__}: {e}")
        return False


def test_download_writes_float(xcp: XcpClient, c_alpha_addr: int):
    """Write 0.75 to c_alpha, verify with SHORT_UPLOAD, restore original."""
    original = struct.unpack("<f", xcp.short_upload(c_alpha_addr, 4))[0]

    xcp.set_mta(c_alpha_addr)
    xcp.download(struct.pack("<f", 0.75))

    readback = struct.unpack("<f", xcp.short_upload(c_alpha_addr, 4))[0]
    assert abs(readback - 0.75) < 1e-6, f"Expected 0.75, got {readback:.6f}"

    # Restore
    xcp.set_mta(c_alpha_addr)
    xcp.download(struct.pack("<f", original))


def test_download_advances_mta(xcp: XcpClient, c_alpha_addr: int):
    """DOWNLOAD should advance MTA by num_bytes. Verify with UPLOAD."""
    # Write 8 bytes (two floats) starting at c_alpha_addr
    val_a = struct.pack("<f", 0.90)
    val_b = struct.pack("<f", 0.80)

    xcp.set_mta(c_alpha_addr)
    xcp.download(val_a)          # MTA advances by 4 → c_alpha_addr + 4
    xcp.download(val_b)          # MTA advances by 4 → c_alpha_addr + 8

    # Read back both with SHORT_UPLOAD
    rb_a = struct.unpack("<f", xcp.short_upload(c_alpha_addr,     4))[0]
    rb_b = struct.unpack("<f", xcp.short_upload(c_alpha_addr + 4, 4))[0]

    assert abs(rb_a - 0.90) < 1e-6, f"val_a: expected 0.90, got {rb_a:.6f}"
    assert abs(rb_b - 0.80) < 1e-6, f"val_b: expected 0.80, got {rb_b:.6f}"

    # Restore c_alpha to 0.98
    xcp.set_mta(c_alpha_addr)
    xcp.download(struct.pack("<f", 0.98))


def test_download_out_of_range_flash(xcp: XcpClient):
    """DOWNLOAD to Flash address must return ERR_OUT_OF_RANGE (0x22)."""
    flash_addr = 0x10000000   # RP2350 Flash base
    xcp.set_mta(flash_addr)
    err = xcp.download_expect_error(struct.pack("<f", 1.0))
    assert err == 0x22, f"Expected ERR_OUT_OF_RANGE (0x22), got 0x{err:02X}"


def test_download_out_of_range_peripheral(xcp: XcpClient):
    """DOWNLOAD to peripheral address must return ERR_OUT_OF_RANGE (0x22)."""
    periph_addr = 0x40000000  # RP2350 peripheral base
    xcp.set_mta(periph_addr)
    err = xcp.download_expect_error(struct.pack("<f", 1.0))
    assert err == 0x22, f"Expected ERR_OUT_OF_RANGE (0x22), got 0x{err:02X}"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 3:
        print(f"Usage: python {sys.argv[0]} <COM port> <elf path or 0xADDRESS>")
        sys.exit(1)

    port = sys.argv[1]
    arg  = sys.argv[2]

    if arg.startswith("0x") or arg.startswith("0X"):
        c_alpha_addr = int(arg, 16)
        print(f"Using c_alpha address: 0x{c_alpha_addr:08X}")
    else:
        print(f"Reading symbol addresses from {arg} ...")
        c_alpha_addr = get_symbol_addr(arg, "c_alpha")

    print()
    print(f"  c_alpha: 0x{c_alpha_addr:08X}\n")

    print(f"Connecting to {port} ...")
    xcp = XcpClient(port)
    xcp.connect()
    print("XCP connected.\n")

    print("DOWNLOAD tests:")
    results = [
        run_test("download writes float to RAM",
                 lambda: test_download_writes_float(xcp, c_alpha_addr)),
        run_test("download advances MTA correctly",
                 lambda: test_download_advances_mta(xcp, c_alpha_addr)),
        run_test("download to Flash rejected (ERR_OUT_OF_RANGE)",
                 lambda: test_download_out_of_range_flash(xcp)),
        run_test("download to peripheral rejected (ERR_OUT_OF_RANGE)",
                 lambda: test_download_out_of_range_peripheral(xcp)),
    ]

    xcp.close()

    passed = sum(results)
    total  = len(results)
    print(f"\n{passed}/{total} tests passed.")
    sys.exit(0 if passed == total else 1)


if __name__ == "__main__":
    main()