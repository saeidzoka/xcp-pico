# Porting Guide

This document describes how to port the xcp-pico protocol stack to a
different microcontroller or ECU. It assumes you have your own application
code already running and want to add XCP calibration/measurement access
to it.

The stack was designed around one principle (ADR-002): `xcp_protocol.c`
knows nothing about your transport, your memory map, or your application
variables. Porting means filling in four things around that core, without
touching it.

---

## What you are NOT porting

`xcp_protocol.c` and `xcp_protocol.h` are the XCP command processor. They
implement CONNECT, DISCONNECT, GET_STATUS, SYNCH, GET_COMM_MODE_INFO,
GET_ID, SET_MTA, UPLOAD, SHORT_UPLOAD, and DOWNLOAD against ASAM MCD-1 XCP
V1.5 Part 1. These two files should not need to change for a port. If you
find yourself editing them, stop and check whether the change belongs in
one of the four files below instead.

---

## Step 1: Implement your transport

`xcp_protocol.c` talks to the outside world exclusively through an
`xcp_transport_ops_t` (defined in `include/xcp_transport.h`):

```c
typedef struct {
    bool (*packet_available)(void);
    bool (*get_packet)(uint8_t *buf, size_t max_len, size_t *actual_len);
    bool (*send_packet)(const uint8_t *data, size_t len);
    bool (*is_connected)(void);
} xcp_transport_ops_t;
```

Implement these four functions for whatever carries XCP packets on your
target: CAN, UART, a different USB stack, Ethernet, SPI. Each function must
be non-blocking and safe to call from your main loop; none may sleep or
busy-wait.

If your transport is a byte stream (UART, SPI, a USB CDC stack other than
tinyusb) rather than a packet-native transport (CAN, Ethernet), you need a
framing layer underneath your `get_packet`/`send_packet` implementation.
xcp-pico's `xcp_transport_sxi` module is a reference implementation of
ASAM's SxI framing (`[LEN][CTR][payload]`) that you can adapt directly; see
the blog post "Bytes Don't Know Where They End" in this project for the
rationale behind that framing choice.

Populate an `xcp_transport_ops_t` with pointers to your four functions and
pass it to `xcp_protocol_init()` at startup. See `main.c` for the reference
pattern (compile-time selection via `#if`/`#elif` on a config flag, all
contained in `main.c` — `xcp_protocol.c` never sees which transport is
active).

---

## Step 2: Fill in your memory map

`xcp_protocol.c` enforces a read/write access policy (RAM-only, currently)
using two facts about your target's memory layout, defined in
`include/xcp_platform.h`:

```c
#define XCP_PLATFORM_RAM_BASE   (0x20000000u)
#define XCP_PLATFORM_RAM_END    (0x20082000u)
```

Replace these with your target's actual SRAM base address and end address
(base + size). Check your MCU's datasheet or linker script for these
values.

If your target has multiple non-contiguous RAM regions (common on larger
MCUs with tightly-coupled memory, backup RAM, etc.), `is_valid_ram_region()`
in `xcp_protocol.c` will need to check all of them. This is the one
legitimate case where you touch `xcp_protocol.c` during a port; keep the
change localised to that one function and export one BASE/END pair per
region from `xcp_platform.h`, following the existing naming pattern.

---

## Step 3: Declare your application variables

`include/xcp_app.h` (or `firmware/src/xcp_app.h` in this project's layout)
is a catalog, not logic. It declares `extern volatile float` (or whatever
types you need) for every variable your XCP master should be able to read
or write. The convention used throughout this project:

- `g_` prefix: measurement variables (read-only from the master's
  perspective — you can still write them internally from your firmware,
  but no calibration tool should need to).
- `c_` prefix: calibration parameters (read/write — tunable live via
  DOWNLOAD).

```c
extern volatile float g_my_sensor_reading;
extern volatile float c_my_tunable_gain;
```

The actual variable definitions live in whichever module owns the data
(a sensor driver, a controller module, etc.), not in this header. This
header exists purely so there is one place a person (or an A2L generator)
can look to see everything XCP-visible in the project.

All variables must be `volatile`. Without it, the compiler is free to
cache a value in a register across loop iterations, and a value written by
the XCP master via DOWNLOAD may never be observed by your application code.

---

## Step 4: Write your A2L

The A2L file is what turns raw memory addresses into a usable calibration
interface. Without it, a master tool has no way to know a variable's name,
type, valid range, or address — SHORT_UPLOAD and DOWNLOAD work by address
only; A2L is what lets a human work by name instead.

`docs/a2l/xcp-pico.a2l` in this project is a working reference. Copy its
structure:

- `MOD_COMMON`: byte order and alignment (copy as-is unless your MCU is
  big-endian or has different alignment requirements).
- `IF_DATA XCP` / `PROTOCOL_LAYER`: must match your `xcp_config.h` values
  (`XCP_MAX_CTO`, `XCP_MAX_DTO`) and the CONNECT response your slave
  actually sends. See the note in the reference A2L about why no standard
  transport `IF_DATA` binding is included for a non-ASAM-standard
  transport like SxI-over-USB-CDC; if you're porting to a standard
  transport (CAN, Ethernet), you should add the matching `XCP_ON_CAN` or
  `XCP_ON_UDP_IP`/`XCP_ON_TCP_IP` block so commercial masters can
  auto-configure the connection.
- `MEASUREMENT` / `CHARACTERISTIC` blocks: one per variable declared in
  your `xcp_app.h`. Addresses come from your compiled ELF.

Addresses in the A2L are RAM addresses assigned by the linker and **will
change** on rebuild. Use `tools/validate_a2l.py` (works against any
project's ELF and A2L, not just this one) to catch drift before it causes
a silent mismatch between what your A2L claims and what your firmware
actually has at that address:

```
python tools/validate_a2l.py path/to/your.elf path/to/your.a2l
```

---

## Checklist

- [ ] `xcp_transport_ops_t` implemented for your transport
- [ ] If byte-stream transport: framing layer implemented (SxI or your own)
- [ ] `xcp_platform.h` updated with your target's real RAM base/end
- [ ] Your own `xcp_app.h` declares every variable you want XCP-visible
- [ ] All XCP-visible variables are `volatile`
- [ ] A2L written, addresses match your compiled ELF
      (`tools/validate_a2l.py` passes)
- [ ] `xcp_protocol.c` and `xcp_protocol.h` unmodified (or changes limited
      to `is_valid_ram_region()` for multi-region memory maps)

If all of these are true, any XCP master — a commercial tool, or this
project's own PC tool once available — that can reach your transport
should be able to CONNECT, read your measurements, and write your
calibration parameters.