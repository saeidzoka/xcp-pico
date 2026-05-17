#!/usr/bin/env python3
"""
XCP protocol layer acceptance tests - Milestone 3
Tests run against real hardware over USB CDC (SxI framing).
Usage: python tools/test_xcp_protocol.py <COM_PORT>
"""

import sys
import struct
import serial
import time

PORT    = sys.argv[1] if len(sys.argv) > 1 else "COM15"
BAUD    = 115200
TIMEOUT = 2.0

PASS = "\033[32mPASS\033[0m"
FAIL = "\033[31mFAIL\033[0m"

results = []

def open_port():
    return serial.Serial(PORT, BAUD, timeout=TIMEOUT)

def sxi_frame(xcp_packet: bytes, ctr: int = 0) -> bytes:
    length = len(xcp_packet)
    header = struct.pack("<HH", length, ctr)
    return header + xcp_packet

def send_and_receive(ser, xcp_packet: bytes, ctr: int = 0) -> bytes:
    ser.reset_input_buffer()
    ser.write(sxi_frame(xcp_packet, ctr))
    # Read SxI header (4 bytes)
    header = ser.read(4)
    if len(header) < 4:
        return b""
    resp_len, _ = struct.unpack("<HH", header)
    payload = ser.read(resp_len)
    return payload

def check(name, condition, detail=""):
    status = PASS if condition else FAIL
    print(f"  [{status}] {name}" + (f" ({detail})" if detail else ""))
    results.append(condition)

def test_connect():
    print("\nTest: CONNECT (0xFF)")
    with open_port() as ser:
        time.sleep(0.1)
        resp = send_and_receive(ser, bytes([0xFF, 0x00]))

    check("response length == 8", len(resp) == 8, f"got {len(resp)}")
    if len(resp) < 8:
        return
    check("PID == 0xFF (positive response)", resp[0] == 0xFF, f"got 0x{resp[0]:02X}")
    check("RESOURCE == 0x01 (CAL/PAG)",      resp[1] == 0x01, f"got 0x{resp[1]:02X}")
    check("COMM_MODE_BASIC == 0x00",          resp[2] == 0x00, f"got 0x{resp[2]:02X}")
    check("MAX_CTO == 64",                    resp[3] == 64,   f"got {resp[3]}")
    max_dto = struct.unpack_from("<H", resp, 4)[0]
    check("MAX_DTO == 64",                    max_dto == 64,   f"got {max_dto}")
    check("PROTOCOL_VERSION == 0x01",         resp[6] == 0x01, f"got 0x{resp[6]:02X}")
    check("TRANSPORT_VERSION == 0x01",        resp[7] == 0x01, f"got 0x{resp[7]:02X}")

def test_unknown_command():
    print("\nTest: unknown command returns ERR_CMD_UNKNOWN")
    with open_port() as ser:
        time.sleep(0.1)
        resp = send_and_receive(ser, bytes([0xF0]))  # unimplemented

    check("response length == 2",             len(resp) == 2,   f"got {len(resp)}")
    if len(resp) < 2:
        return
    check("PID == 0xFE (negative response)",  resp[0] == 0xFE,  f"got 0x{resp[0]:02X}")
    check("ERR == 0x20 (ERR_CMD_UNKNOWN)",    resp[1] == 0x20,  f"got 0x{resp[1]:02X}")


def test_disconnect():
    print("\nTest: DISCONNECT (0xFE)")
    with open_port() as ser:
        time.sleep(0.1)
        # First connect
        send_and_receive(ser, bytes([0xFF, 0x00]))
        # Then disconnect
        resp = send_and_receive(ser, bytes([0xFE, 0x00]))

    check("response length == 1", len(resp) == 1, f"got {len(resp)}")
    if len(resp) < 1:
        return
    check("PID == 0xFF (positive response)", resp[0] == 0xFF, f"got 0x{resp[0]:02X}")


def test_get_status():
    print("\nTest: GET_STATUS (0xFD)")
    with open_port() as ser:
        time.sleep(0.1)
        resp = send_and_receive(ser, bytes([0xFD]))

    check("response length == 6",            len(resp) == 6,   f"got {len(resp)}")
    if len(resp) < 6:
        return
    check("PID == 0xFF (positive response)", resp[0] == 0xFF,  f"got 0x{resp[0]:02X}")
    check("SESSION_STATUS == 0x00",          resp[1] == 0x00,  f"got 0x{resp[1]:02X}")
    check("RESOURCE_PROTECTION == 0x00",     resp[2] == 0x00,  f"got 0x{resp[2]:02X}")


def test_synch():
    print("\nTest: SYNCH (0xFC)")
    with open_port() as ser:
        time.sleep(0.1)
        resp = send_and_receive(ser, bytes([0xFC]))

    check("response length == 2",            len(resp) == 2,   f"got {len(resp)}")
    if len(resp) < 2:
        return
    check("PID == 0xFE (negative response)", resp[0] == 0xFE,  f"got 0x{resp[0]:02X}")
    check("ERR == 0x00 (ERR_CMD_SYNCH)",     resp[1] == 0x00,  f"got 0x{resp[1]:02X}")


def test_get_comm_mode_info():
    print("\nTest: GET_COMM_MODE_INFO (0xFB)")
    with open_port() as ser:
        time.sleep(0.1)
        resp = send_and_receive(ser, bytes([0xFB]))

    check("response length == 8",            len(resp) == 8,   f"got {len(resp)}")
    if len(resp) < 8:
        return
    check("PID == 0xFF (positive response)", resp[0] == 0xFF,  f"got 0x{resp[0]:02X}")
    check("COMM_MODE_OPTIONAL == 0x00",      resp[2] == 0x00,  f"got 0x{resp[2]:02X}")
    check("XCP_DRIVER_VERSION == 0x01",      resp[7] == 0x01,  f"got 0x{resp[7]:02X}")


def test_get_id():
    print("\nTest: GET_ID (0xFA)")
    with open_port() as ser:
        time.sleep(0.1)
        resp = send_and_receive(ser, bytes([0xFA, 0x01]))  # type = 0x01 (ASCII)

    check("response length == 16",           len(resp) == 16,  f"got {len(resp)}")
    if len(resp) < 16:
        return
    check("PID == 0xFF (positive response)", resp[0] == 0xFF,  f"got 0x{resp[0]:02X}")
    check("Mode == 0x01 (inline data)",      resp[1] == 0x01,  f"got 0x{resp[1]:02X}")
    id_len = struct.unpack_from("<I", resp, 4)[0]
    check("ID length == 8",                  id_len == 8,      f"got {id_len}")
    station_id = resp[8:8 + id_len].decode("ascii", errors="replace")
    check("station ID == 'xcp-pico'",        station_id == "xcp-pico", f"got '{station_id}'")
# -------------------------------------------------------------------------

print(f"XCP Protocol Tests on {PORT}")
print("=" * 40)

test_connect()
test_disconnect()
test_get_status()
test_synch()
test_get_comm_mode_info()
test_get_id()
test_unknown_command()

total  = len(results)
passed = sum(results)
color  = "\033[32m" if passed == total else "\033[31m"
print(f"\n{color}{passed} / {total} tests passed\033[0m")
sys.exit(0 if passed == total else 1)