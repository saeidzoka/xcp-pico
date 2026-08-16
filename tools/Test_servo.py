#!/usr/bin/env python3
"""
test_servo.py - Sweep SG90 servo via XCP DOWNLOAD.

Moves the servo from -90 to +90 and back in a loop.
Press Ctrl+C to stop (servo returns to 0 degrees).

Usage:
    python tools/test_servo.py COM15
"""

import serial
import struct
import sys
import time


C_SERVO_ANGLE_ADDR = 0x20001990


class XcpClient:
    def __init__(self, port: str):
        self.ser = serial.Serial(port, 115200, timeout=1.0)
        self.ctr = 0
        time.sleep(0.5)
        self.ser.reset_input_buffer()

    def _send(self, payload: bytes) -> bytes:
        header = struct.pack("<HH", len(payload), self.ctr)
        self.ser.write(header + payload)
        self.ctr += 1
        resp_header = self.ser.read(4)
        if len(resp_header) < 4:
            raise TimeoutError("No response")
        resp_len, _ = struct.unpack("<HH", resp_header)
        return self.ser.read(resp_len)

    def connect(self):
        resp = self._send(bytes([0xFF, 0x00]))
        assert resp[0] == 0xFF, f"CONNECT failed: 0x{resp[0]:02X}"

    def set_angle(self, angle_deg: float):
        """Write angle to c_servo_angle via SET_MTA + DOWNLOAD."""
        # SET_MTA
        cmd = struct.pack("<BBBBI", 0xF6, 0x00, 0x00, 0x00, C_SERVO_ANGLE_ADDR)
        resp = self._send(cmd)
        assert resp[0] == 0xFF

        # DOWNLOAD 4 bytes (float)
        data = struct.pack("<f", angle_deg)
        cmd  = bytes([0xF0, 4]) + data
        resp = self._send(cmd)
        assert resp[0] == 0xFF

    def close(self):
        self.set_angle(0.0)
        self.ser.close()


def main():
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <COM port>")
        sys.exit(1)

    xcp = XcpClient(sys.argv[1])
    xcp.connect()
    print("XCP connected. Sweeping servo... (Ctrl+C to stop)\n")

    try:
        while True:
            for angle in range(-90, 91, 5):
                xcp.set_angle(float(angle))
                print(f"  angle: {angle:+4d} deg", end="\r")
                time.sleep(0.05)

            for angle in range(90, -91, -5):
                xcp.set_angle(float(angle))
                print(f"  angle: {angle:+4d} deg", end="\r")
                time.sleep(0.05)

    except KeyboardInterrupt:
        print("\n\nStopping. Returning to 0 degrees.")
    finally:
        xcp.close()


if __name__ == "__main__":
    main()