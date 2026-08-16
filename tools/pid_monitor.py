#!/usr/bin/env python3
"""
pid_monitor.py - Live XCP readout of PID controller state.

Shows g_pitch_deg (from MPU-6050) and g_pid_output side by side.
Tilt the sensor and watch the PID output respond, even without the
servo connected. Optionally set Kp/Ki/Kd/setpoint live.

Usage:
    python tools/pid_monitor.py COM15
    python tools/pid_monitor.py COM15 --kp 2.0 --ki 0.1 --kd 0.05
"""

import serial
import struct
import sys
import time
import os
import argparse


ADDR = {
    "g_pitch_deg":  0x20001A34,
    "g_pid_output": 0x20001A30,
    "c_Kp":         0x20001184,
    "c_Ki":         0x200019A4,
    "c_Kd":         0x200019A0,
    "c_setpoint":   0x200019A8,
}


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
            raise TimeoutError("No response from slave")
        resp_len, _ = struct.unpack("<HH", resp_header)
        payload = self.ser.read(resp_len)
        if len(payload) < resp_len:
            raise TimeoutError(f"Incomplete response: {len(payload)}/{resp_len}")
        return payload

    def connect(self):
        resp = self._send(bytes([0xFF, 0x00]))
        assert resp[0] == 0xFF, f"CONNECT failed: 0x{resp[0]:02X}"

    def short_upload(self, addr: int) -> float:
        cmd = struct.pack("<BBBBI", 0xF4, 4, 0x00, 0x00, addr)
        resp = self._send(cmd)
        assert resp[0] == 0xFF, f"SHORT_UPLOAD failed: 0x{resp[0]:02X}"
        return struct.unpack("<f", resp[1:5])[0]

    def write_float(self, addr: int, value: float):
        cmd = struct.pack("<BBBBI", 0xF6, 0x00, 0x00, 0x00, addr)
        resp = self._send(cmd)
        assert resp[0] == 0xFF, f"SET_MTA failed: 0x{resp[0]:02X}"

        data = struct.pack("<f", value)
        cmd  = bytes([0xF0, 4]) + data
        resp = self._send(cmd)
        assert resp[0] == 0xFF, f"DOWNLOAD failed: 0x{resp[0]:02X}"

    def close(self):
        self.ser.close()


def clear():
    os.system("cls" if os.name == "nt" else "clear")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("--kp", type=float, default=None)
    parser.add_argument("--ki", type=float, default=None)
    parser.add_argument("--kd", type=float, default=None)
    parser.add_argument("--setpoint", type=float, default=None)
    args = parser.parse_args()

    xcp = XcpClient(args.port)
    xcp.connect()
    print("XCP connected.\n")

    if args.kp is not None:
        xcp.write_float(ADDR["c_Kp"], args.kp)
        print(f"c_Kp set to {args.kp}")
    if args.ki is not None:
        xcp.write_float(ADDR["c_Ki"], args.ki)
        print(f"c_Ki set to {args.ki}")
    if args.kd is not None:
        xcp.write_float(ADDR["c_Kd"], args.kd)
        print(f"c_Kd set to {args.kd}")
    if args.setpoint is not None:
        xcp.write_float(ADDR["c_setpoint"], args.setpoint)
        print(f"c_setpoint set to {args.setpoint}")

    time.sleep(0.3)

    kp = xcp.short_upload(ADDR["c_Kp"])
    ki = xcp.short_upload(ADDR["c_Ki"])
    kd = xcp.short_upload(ADDR["c_Kd"])
    sp = xcp.short_upload(ADDR["c_setpoint"])

    print("\nLive PID monitor. Tilt the MPU-6050. Ctrl+C to stop.\n")
    time.sleep(1.0)

    try:
        while True:
            pitch  = xcp.short_upload(ADDR["g_pitch_deg"])
            output = xcp.short_upload(ADDR["g_pid_output"])

            clear()
            print("xcp-pico  PID live monitor   (Ctrl+C to quit)\n")
            print(f"  Gains:  Kp={kp:.3f}  Ki={ki:.3f}  Kd={kd:.3f}  setpoint={sp:.2f} deg\n")
            print(f"  {'g_pitch_deg':<16}: {pitch:>8.2f} deg   <-- tilt me")
            print(f"  {'g_pid_output':<16}: {output:>8.2f}     (would drive servo)")

            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        xcp.close()


if __name__ == "__main__":
    main()