# xcp-pico Architecture

This document describes the high-level architecture of `xcp-pico`. Specific design decisions are recorded as ADRs in [`docs/adr/`](adr/).

## Goals and non-goals

### Goals

- **Spec compliance.** The slave implementation must conform to ASAM XCP V1.5 for the implemented command subset. Behavior should be predictable to any standard XCP master (e.g. Vector CANape, ETAS INCA).
- **Transport portability.** Adding a new transport (UART, SPI, CAN) must not require changes to the protocol layer or application layer.
- **Determinism in the slave.** The embedded target must have bounded memory, no dynamic allocation in the protocol layer, and predictable timing.
- **Approachability.** The codebase should be readable by engineers new to XCP, with clear module boundaries and inline references to spec sections.

### Non-goals (for now)

- Full XCP V1.5 command coverage. Phase 1 implements only the minimum command set required for measurement and basic calibration.
- DAQ (Data Acquisition) lists. Cyclic measurement is a Phase 2 concern.
- PGM (Programming) commands. Flash programming is out of scope.
- Multi-master support. The slave assumes a single connected master.

## System overview

The system has three top-level components:

1. **PC Tool (XCP Master).** A Python application that parses A2L files, presents a GUI, and implements the master side of the XCP protocol.
2. **Embedded Target (XCP Slave).** Firmware running on the RP2350, implementing the slave side of the protocol and exposing a calibratable application.
3. **Reference Application.** A PID attitude controller using the MPU-6050 IMU. This serves as a realistic calibration target with tunable parameters and measurable signals.

```
+---------------------+               +---------------------+
|       PC Host       |               |  Raspberry Pi Pico 2 |
|                     |               |                     |
|   Python PC Tool    |  USB CDC      |   xcp-pico Firmware |
|   (XCP Master)      |  <=========>  |   (XCP Slave + App) |
|                     |  byte stream  |                     |
+---------------------+               +---------------------+
```

## The three-layer slave architecture

The embedded target is split into three strictly-separated layers, mirroring the architecture used in production-grade automotive XCP stacks (e.g. Vector MICROSAR XCP).

```
+--------------------------------------------+
|         Application Layer                  |
|  (PID controller, MPU-6050 driver,         |
|   measurement signals, calibration params) |
+--------------------------------------------+
                     ^
                     | XCP API
                     | (read/write memory at MTA)
                     v
+--------------------------------------------+
|          Protocol Layer                    |
|  (Command interpreter, state machine,      |
|   MTA tracking, response builder)          |
+--------------------------------------------+
                     ^
                     | Packet API
                     | (send/receive XCP packet)
                     v
+--------------------------------------------+
|         Transport Layer                    |
|  (SxI framing: LEN + CTR + packet,         |
|   USB CDC byte stream handling)            |
+--------------------------------------------+
                     ^
                     | Byte stream
                     v
                [USB CDC hardware]
```

### Transport Layer

**Responsibility:** Convert the underlying byte-oriented or message-oriented transport into discrete XCP packets, and vice versa.

**Inputs:** Raw bytes from USB CDC.
**Outputs:** Complete XCP packets (with their length) delivered to the protocol layer.

**Key concerns:**
- Framing (where does one packet start and end?)
- Counter management (`CTR` field for sequence detection)
- Buffer management (handling partial reads on a streaming transport)

For Phase 1, this layer implements **XCP on SxI framing over USB CDC**. See [`ADR-001`](adr/001-xcp-on-sxi-over-usb-cdc.md).

### Protocol Layer

**Responsibility:** Implement the XCP protocol state machine and command interpreter, independent of any specific transport.

**Inputs:** XCP packets from the transport layer.
**Outputs:** XCP response packets to the transport layer; read/write requests to the application layer (via the XCP API).

**Key concerns:**
- Command dispatch (`CONNECT`, `UPLOAD`, `DOWNLOAD`, etc.)
- Session state (connected / disconnected, MTA pointer, resource protection)
- Response formatting (positive response, error response with error codes)

This layer **must not** know whether bytes arrive over USB, UART, or CAN. That separation is what makes the same protocol layer reusable across Phase 1 (USB CDC) and Phase 2 (CAN).

### Application Layer

**Responsibility:** Provide the calibratable and measurable variables, and contain the actual product logic (in our case, the PID controller).

**Inputs:** Sensor data (MPU-6050 over I2C); calibration writes from the protocol layer.
**Outputs:** Control signals; measurement variables that the protocol layer can read.

**Key concerns:**
- Defining the memory layout of calibration parameters and measurement signals (this layout must match the A2L file)
- Running the actual control loop deterministically

The application layer is what differs between products. The transport and protocol layers are reusable infrastructure.

## A typical request flow

To make the layering concrete, here is what happens when the master sends a `SHORT_UPLOAD` request (read N bytes from a memory address):

1. **PC Tool** builds the XCP packet `[0xF4, n, 0x00, ext, addr32]`, frames it as `[LEN, CTR, packet]` (SxI framing), and writes the bytes to the USB CDC endpoint.
2. **Transport Layer** receives bytes from CDC. It accumulates them until it has a full SxI frame (LEN bytes following the header). It strips the LEN/CTR header and hands the raw XCP packet up to the protocol layer.
3. **Protocol Layer** dispatches on the command code (`0xF4` = `SHORT_UPLOAD`). It validates the request (are we connected? is the address valid?), then calls into the application layer's read function with the requested address and length.
4. **Application Layer** returns the requested bytes from its memory map.
5. **Protocol Layer** builds a positive response packet `[0xFF, data...]`.
6. **Transport Layer** wraps the response in an SxI frame (with an incremented CTR) and writes it to the CDC endpoint.
7. **PC Tool** receives the bytes, deframes them, validates the CTR, and delivers the response payload to the master logic.

This flow shows why strict layering matters: the protocol layer doesn't care that this is USB, and the application layer doesn't care that this is XCP. Each layer has one job.

## Concurrency model on RP2350

The RP2350 has two Cortex-M33 cores. The current plan is:

- **Core 0:** USB CDC handling, transport layer, protocol layer.
- **Core 1:** Application layer (PID loop, sensor reads).

Inter-core communication uses lock-free single-producer single-consumer queues for measurement data, and a mutex-protected calibration parameter region. The exact synchronization strategy will be detailed in a future ADR (likely `ADR-003`).

## PC Tool architecture

(Detailed design pending. Initial sketch:)

- **A2L Parser:** Parses the A2L description file (variable layout, conversion rules, limits) and exposes a structured Python representation.
- **Master Protocol:** Implements XCP master-side state machine and command building, mirroring the slave's protocol layer.
- **Transport:** Handles USB CDC (via `pyserial`) in Phase 1, with the same abstraction strategy as the slave to allow CAN later.
- **GUI:** TBD. Likely PySide6 or a web frontend, depending on Phase 1 outcomes.

## Open questions

- Should the protocol layer be table-driven (command dispatch table) or switch-driven? Table-driven is more extensible; switch is more debuggable. Will decide in a future ADR.
- How to handle the boundary between calibration RAM and code flash on RP2350, given that all calibration is in RAM in this project (no XCP page switching)? Likely fine for now but worth documenting.
- A2L generation strategy: hand-written for Phase 1, but eventually we want automation. Out of scope for now.