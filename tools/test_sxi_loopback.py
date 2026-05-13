#!/usr/bin/env python3
"""
xcp-pico Milestone 2 acceptance tests.

Exercises the SxI framing layer over USB CDC by sending crafted
frames to the firmware loopback and verifying the echoed response.

Usage:
    python test_sxi_loopback.py COM14
    python test_sxi_loopback.py /dev/ttyACM0

The firmware must be flashed with the Milestone 2 frame-level
loopback (commit d47d223 or later).
"""

import argparse
import struct
import sys
import time

import serial


# --------------------------------------------------------------------
# Frame helpers
# --------------------------------------------------------------------

SXI_HEADER_SIZE = 4
XCP_MAX_CTO = 64


def build_frame(payload: bytes, ctr: int) -> bytes:
    """Wrap a payload in an SxI frame: [LEN(2)][CTR(2)][payload]."""
    if len(payload) > 0xFFFF:
        raise ValueError("payload too large for 16-bit LEN field")
    return struct.pack("<HH", len(payload), ctr) + payload


def parse_frame(data: bytes) -> tuple[int, int, bytes]:
    """Decompose [LEN(2)][CTR(2)][payload] into (len, ctr, payload)."""
    if len(data) < SXI_HEADER_SIZE:
        raise ValueError(f"frame too short: {len(data)} bytes")
    length, ctr = struct.unpack("<HH", data[:SXI_HEADER_SIZE])
    payload = data[SXI_HEADER_SIZE:SXI_HEADER_SIZE + length]
    if len(payload) != length:
        raise ValueError(
            f"payload truncated: expected {length}, got {len(payload)}"
        )
    return length, ctr, payload


def read_frame(ser: serial.Serial, timeout: float = 1.0) -> bytes:
    """Read one full SxI frame from the serial port.

    Reads the 4-byte header first, then the payload bytes indicated
    by LEN. Raises TimeoutError if no complete frame arrives within
    the timeout window.
    """
    deadline = time.monotonic() + timeout
    header = b""
    while len(header) < SXI_HEADER_SIZE:
        if time.monotonic() > deadline:
            raise TimeoutError(
                f"header timeout: got {len(header)} of {SXI_HEADER_SIZE} bytes"
            )
        chunk = ser.read(SXI_HEADER_SIZE - len(header))
        header += chunk

    length = struct.unpack("<H", header[:2])[0]

    payload = b""
    while len(payload) < length:
        if time.monotonic() > deadline:
            raise TimeoutError(
                f"payload timeout: got {len(payload)} of {length} bytes"
            )
        chunk = ser.read(length - len(payload))
        payload += chunk

    return header + payload


# --------------------------------------------------------------------
# Test framework
# --------------------------------------------------------------------

class TestResult:
    """Container for individual test outcomes."""

    def __init__(self, name: str):
        self.name = name
        self.passed = False
        self.message = ""


def colour(text: str, code: str) -> str:
    """Wrap text in ANSI colour codes for terminal output."""
    return f"\033[{code}m{text}\033[0m"


def green(text: str) -> str:
    return colour(text, "32")


def red(text: str) -> str:
    return colour(text, "31")


def yellow(text: str) -> str:
    return colour(text, "33")


def run_test(name: str, test_fn) -> TestResult:
    """Run a single test function and capture its result."""
    result = TestResult(name)
    print(f"  {yellow('RUN ')} {name}", end="", flush=True)
    try:
        test_fn()
        result.passed = True
        print(f"\r  {green('PASS')} {name}")
    except AssertionError as e:
        result.message = str(e)
        print(f"\r  {red('FAIL')} {name}")
        print(f"         {e}")
    except Exception as e:
        result.message = f"{type(e).__name__}: {e}"
        print(f"\r  {red('ERR ')} {name}")
        print(f"         {type(e).__name__}: {e}")
    return result


# --------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------

class LoopbackTests:
    """Test suite against the running firmware.

    A single serial connection is shared across all tests so that
    tx_ctr accumulates monotonically and we can verify counter
    behaviour across test boundaries.
    """

    def __init__(self, ser: serial.Serial):
        self.ser = ser
        self.expected_rx_ctr = 0  # what we expect from the slave

    def _send_and_recv(self, payload: bytes, tx_ctr: int = 0) -> tuple[int, int, bytes]:
        """Send one frame, read one frame back, return parsed components."""
        frame = build_frame(payload, tx_ctr)
        self.ser.write(frame)
        response = read_frame(self.ser)
        return parse_frame(response)

    # ----- Test 1: basic round-trip -----
    def test_basic_round_trip(self):
        payload = b"HELL"
        length, ctr, echoed = self._send_and_recv(payload, tx_ctr=0)

        assert length == len(payload), \
            f"length mismatch: sent {len(payload)}, got {length}"
        assert echoed == payload, \
            f"payload mismatch: sent {payload!r}, got {echoed!r}"
        assert ctr == self.expected_rx_ctr, \
            f"CTR mismatch: expected {self.expected_rx_ctr}, got {ctr}"
        self.expected_rx_ctr += 1

    # ----- Test 2: CTR increments monotonically -----
    def test_ctr_increment(self):
        for i in range(5):
            payload = bytes([0xA0 + i, 0xB0 + i])
            _, ctr, echoed = self._send_and_recv(payload, tx_ctr=100 + i)

            assert echoed == payload, \
                f"iteration {i}: payload mismatch"
            assert ctr == self.expected_rx_ctr, \
                f"iteration {i}: CTR {ctr} != expected {self.expected_rx_ctr}"
            self.expected_rx_ctr += 1

    # ----- Test 3: various payload sizes -----
    def test_various_payload_sizes(self):
        for size in [1, 8, 16, 32, 63]:
            payload = bytes(range(size))
            length, ctr, echoed = self._send_and_recv(payload, tx_ctr=200)

            assert length == size, \
                f"size {size}: length mismatch, got {length}"
            assert echoed == payload, \
                f"size {size}: payload mismatch"
            assert ctr == self.expected_rx_ctr, \
                f"size {size}: CTR {ctr} != expected {self.expected_rx_ctr}"
            self.expected_rx_ctr += 1

    # ----- Test 4: maximum payload (XCP_MAX_CTO) -----
    def test_maximum_payload(self):
        payload = bytes(range(XCP_MAX_CTO))  # 64 bytes, 0x00..0x3F
        length, ctr, echoed = self._send_and_recv(payload, tx_ctr=300)

        assert length == XCP_MAX_CTO, \
            f"max payload length mismatch: got {length}"
        assert echoed == payload, \
            "max payload content mismatch"
        assert ctr == self.expected_rx_ctr, \
            f"max payload CTR mismatch: got {ctr}"
        self.expected_rx_ctr += 1

    # ----- Test 5: idle timeout recovery -----
    def test_idle_timeout_recovery(self):
        # Send only the first two bytes of a header (partial LEN).
        # No CTR, no payload. Firmware should sit in WAIT_HEADER
        # with rx_index=2 and then time out.
        self.ser.write(b"\x04\x00")

        # Wait well beyond the 50ms idle timeout in firmware.
        time.sleep(0.2)

        # Now send a full, valid frame. Firmware must have reset
        # its parser, otherwise the partial header would corrupt
        # the new frame's LEN/CTR interpretation.
        payload = b"OK"
        length, ctr, echoed = self._send_and_recv(payload, tx_ctr=400)

        assert length == len(payload), \
            f"post-recovery length mismatch: got {length}"
        assert echoed == payload, \
            f"post-recovery payload mismatch: got {echoed!r}"
        assert ctr == self.expected_rx_ctr, \
            f"post-recovery CTR mismatch: got {ctr}"
        self.expected_rx_ctr += 1

    # ----- Test 6: oversized LEN rejection -----
    def test_oversized_len_rejection(self):
        # Craft a frame with LEN > XCP_MAX_CTO. Firmware should
        # discard it and remain ready for the next frame.
        oversized = struct.pack("<HH", 1000, 500) + b"X" * 4
        self.ser.write(oversized)

        # Give firmware time to read those 8 bytes, hit the
        # framing_errors path, reset, and discard any remaining
        # bytes via idle timeout.
        time.sleep(0.2)

        # Drain anything that might have leaked back (there
        # shouldn't be anything, but be safe).
        self.ser.reset_input_buffer()

        # Send a valid frame and verify normal operation resumes.
        payload = b"GOOD"
        length, ctr, echoed = self._send_and_recv(payload, tx_ctr=500)

        assert length == len(payload), \
            f"post-rejection length mismatch: got {length}"
        assert echoed == payload, \
            f"post-rejection payload mismatch: got {echoed!r}"
        assert ctr == self.expected_rx_ctr, \
            f"post-rejection CTR mismatch: got {ctr}"
        self.expected_rx_ctr += 1


# --------------------------------------------------------------------
# Main
# --------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(
        description="xcp-pico Milestone 2 SxI loopback tests"
    )
    parser.add_argument("port", help="serial port (e.g. COM14, /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="baud rate (default: 115200)")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(red(f"Cannot open {args.port}: {e}"))
        return 2

    # Give the firmware a moment to settle after the host opens
    # the port. tinyusb sometimes needs a tick to fully connect.
    time.sleep(0.5)
    ser.reset_input_buffer()

    print(f"Connected to {args.port} at {args.baud} baud")
    print()

    suite = LoopbackTests(ser)
    tests = [
        ("basic round-trip",          suite.test_basic_round_trip),
        ("CTR increment",             suite.test_ctr_increment),
        ("various payload sizes",     suite.test_various_payload_sizes),
        ("maximum payload",           suite.test_maximum_payload),
        ("idle timeout recovery",     suite.test_idle_timeout_recovery),
        ("oversized LEN rejection",   suite.test_oversized_len_rejection),
    ]

    results = [run_test(name, fn) for name, fn in tests]

    ser.close()

    print()
    passed = sum(1 for r in results if r.passed)
    total = len(results)
    summary = f"{passed} / {total} tests passed"
    if passed == total:
        print(green(summary))
        return 0
    else:
        print(red(summary))
        return 1


if __name__ == "__main__":
    sys.exit(main())