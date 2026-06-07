#!/usr/bin/env python3
"""
mpu6050_monitor.py - Live XCP readout of MPU-6050 sensor data.

Connects to xcp-pico via USB CDC (SxI framing) and reads all
MPU-6050 variables every 100 ms. Tilt the sensor to see g_pitch_deg change.

Usage:
    python tools/mpu6050_monitor.py COM15

Addresses are taken directly from:
    arm-none-eabi-nm firmware\\build\\xcp_pico.elf | Select-String "g_pitch|g_accel|g_gyro|c_alpha"
"""

import serial
import struct
import time
import sys
import os

# ---------------------------------------------------------------------------
# Variable addresses (from nm output - update if you recompile)
# ---------------------------------------------------------------------------
VARS = {
    "c_alpha":     0x20001180,
    "g_accel_x":   0x200019FC,
    "g_accel_y":   0x20001A00,
    "g_accel_z":   0x20001A04,
    "g_gyro_x":    0x20001A08,
    "g_gyro_y":    0x20001A0C,
    "g_gyro_z":    0x20001A10,
    "g_pitch_deg": 0x20001A14,
}

BAUD    = 115200
TIMEOUT = 1.0

# ---------------------------------------------------------------------------
# SxI framing
# Frame format: [LEN(2LE)][CTR(2LE)][XCP payload]
# ---------------------------------------------------------------------------

def sxi_send(ser: serial.Serial, payload: bytes, ctr: int) -> None:
    header = struct.pack("<HH", len(payload), ctr)
    ser.write(header + payload)


def sxi_recv(ser: serial.Serial) -> bytes:
    header = ser.read(4)
    if len(header) < 4:
        raise TimeoutError("No response from slave. Is the firmware running?")
    resp_len, _ = struct.unpack("<HH", header)
    payload = ser.read(resp_len)
    if len(payload) < resp_len:
        raise TimeoutError(f"Incomplete response: got {len(payload)}/{resp_len} bytes")
    return payload


def xcp_cmd(ser: serial.Serial, payload: bytes, ctr: int) -> bytes:
    sxi_send(ser, payload, ctr)
    resp = sxi_recv(ser)
    if resp[0] == 0xFE:
        raise RuntimeError(f"XCP negative response: error code 0x{resp[1]:02X}")
    if resp[0] != 0xFF:
        raise RuntimeError(f"Unexpected response PID: 0x{resp[0]:02X}")
    return resp

# ---------------------------------------------------------------------------
# XCP commands
# ---------------------------------------------------------------------------

def xcp_connect(ser: serial.Serial, ctr: int) -> int:
    xcp_cmd(ser, bytes([0xFF, 0x00]), ctr)
    return ctr + 1


def xcp_short_upload(ser: serial.Serial, addr: int, num_bytes: int,
                     ctr: int) -> tuple[bytes, int]:
    """SHORT_UPLOAD: read num_bytes from addr without changing MTA."""
    cmd = struct.pack("<BBBBI", 0xF4, num_bytes, 0x00, 0x00, addr)
    resp = xcp_cmd(ser, cmd, ctr)
    return resp[1:1 + num_bytes], ctr + 1


def read_float(ser: serial.Serial, addr: int, ctr: int) -> tuple[float, int]:
    data, ctr = xcp_short_upload(ser, addr, 4, ctr)
    return struct.unpack("<f", data)[0], ctr

# ---------------------------------------------------------------------------
# Display
# ---------------------------------------------------------------------------

def clear():
    os.system("cls" if os.name == "nt" else "clear")


def print_table(values: dict) -> None:
    clear()
    print("xcp-pico  MPU-6050 live monitor   (Ctrl+C to quit)\n")
    print(f"  {'Variable':<14}  {'Value':>10}  {'Unit'}")
    print("  " + "-" * 36)

    units = {
        "g_accel_x":   "g",
        "g_accel_y":   "g",
        "g_accel_z":   "g",
        "g_gyro_x":    "deg/s",
        "g_gyro_y":    "deg/s",
        "g_gyro_z":    "deg/s",
        "g_pitch_deg": "deg",
        "c_alpha":     "(filter coeff)",
    }

    for name, val in values.items():
        unit = units.get(name, "")
        marker = " <-- tilt me" if name == "g_pitch_deg" else ""
        print(f"  {name:<14}  {val:>10.4f}  {unit}{marker}")

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print(f"Usage: python {sys.argv[0]} <COM port>")
        print(f"Example: python {sys.argv[0]} COM15")
        sys.exit(1)

    port = sys.argv[1]

    print(f"Opening {port} ...")
    ser = serial.Serial(port, BAUD, timeout=TIMEOUT)
    time.sleep(0.5)
    ser.reset_input_buffer()

    ctr = 0
    ctr = xcp_connect(ser, ctr)
    print("XCP connected. Reading sensor data...\n")
    time.sleep(0.2)

    try:
        while True:
            values = {}
            for name, addr in VARS.items():
                val, ctr = read_float(ser, addr, ctr)
                values[name] = val

            print_table(values)
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nStopped.")
    except Exception as e:
        print(f"\nError: {e}")
    finally:
        ser.close()


if __name__ == "__main__":
    main()