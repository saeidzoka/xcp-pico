# xcp-pico

> An open-source XCP (ASAM MCD-1) calibration system for embedded ECUs, running on the Raspberry Pi Pico 2.

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Status: Phase 1](https://img.shields.io/badge/Status-Phase%201-yellow.svg)](#roadmap)
[![Platform: RP2350](https://img.shields.io/badge/Platform-RP2350-purple.svg)](https://www.raspberrypi.com/products/rp2350/)

## What is this?

`xcp-pico` is an open-source implementation of the **XCP (Universal Measurement and Calibration Protocol)** as defined by the ASAM MCD-1 standard, targeting the dual-core Cortex-M33 Raspberry Pi Pico 2 (RP2350).

XCP is the de-facto industry protocol for ECU calibration, measurement, and flashing in automotive and motorsport. Commercial XCP toolchains typically cost £25,000 or more, putting them out of reach for Formula Student teams, hobbyist tuners, and independent engineers.

This project aims to change that by providing:

- A spec-compliant XCP slave (firmware) running on a £6 microcontroller
- A Python-based XCP master with A2L parser and GUI (PC tool)
- A reference application (PID controller using an MPU-6050 IMU) demonstrating real-world calibration

## Status

**Currently in active development. Phase 1 (USB CDC transport) is in progress.**

This is not yet production-ready. See the [Roadmap](#roadmap) below for current state.

## Architecture

The system follows the standard three-layer XCP architecture:

```
+--------------------+         +--------------------+
|     PC Tool        |         |  Embedded Target   |
|   (XCP Master)     |         |    (XCP Slave)     |
|                    |         |                    |
|  +--------------+  |         |  +--------------+  |
|  |  A2L Parser  |  |         |  | Application  |  |
|  +--------------+  |         |  |  (PID + IMU) |  |
|  +--------------+  |         |  +--------------+  |
|  |     GUI      |  |         |  +--------------+  |
|  +--------------+  |         |  |   Protocol   |  |
|  +--------------+  |   USB   |  |    Layer     |  |
|  | Master Logic | <==CDC==>  |  +--------------+  |
|  +--------------+  |         |  +--------------+  |
|                    |         |  |  Transport   |  |
|                    |         |  |    Layer     |  |
|                    |         |  +--------------+  |
+--------------------+         +--------------------+
```

For details, see [`docs/architecture.md`](docs/architecture.md).

## Why XCP on USB CDC (and not "XCP on USB")?

The ASAM XCP V1.5 specification states that "XCP on USB has no practical significance." Instead of implementing the full USB transport binding, this project uses **XCP on SxI framing carried over a USB CDC byte stream**.

This decision is documented in [`ADR-001`](docs/adr/001-xcp-on-sxi-over-usb-cdc.md). It allows future migration to UART or SPI transports without touching the protocol layer.

## Roadmap

### Phase 1: USB CDC Transport (current)

- [x] Milestone 0: Toolchain and "Hello World" on Pico 2
- [ ] Milestone 1: USB CDC byte stream baseline
- [ ] Milestone 2: Transport Layer with SxI framing (LEN + CTR + state machine)
- [ ] Milestone 3: Protocol Layer with minimum command set (`CONNECT`, `DISCONNECT`, `GET_STATUS`, `GET_COMM_MODE_INFO`, `SET_MTA`, `UPLOAD`, `SHORT_UPLOAD`, `DOWNLOAD`)
- [ ] Milestone 4: Application Layer (PID controller + manual A2L file)

### Phase 2: CAN Transport

- [ ] CAN bus support via MCP2515 SPI controller
- [ ] XCP on CAN binding

### Phase 3 (exploratory)

- AI-assisted calibration tooling
- Additional reference applications

## Getting Started

### Prerequisites

You will need the following installed on your development machine:

- **Raspberry Pi Pico SDK v2.2.0 or later** (with RP2350 support). Set the `PICO_SDK_PATH` environment variable to point to your SDK installation.
- **arm-none-eabi-gcc** toolchain
- **CMake 3.13+** and **Ninja**
- **Python 3.10+** for the PC tool
- (Optional) **J-Link** or **picoprobe** for SWD debugging

### Hardware

- Raspberry Pi Pico 2 (RP2350)
- USB cable for CDC communication
- (Optional) SWD debugger for development

### Build

```bash
cd firmware
mkdir build && cd build
cmake -G Ninja ..
ninja
```

The output `xcp_pico.uf2` can be flashed via BOOTSEL mode (drag-and-drop) or SWD.

### Running the PC tool

(Coming soon. Will be installable via `pip install -e pc-tool/` once Phase 1 stabilizes.)

## Contributing

This is currently a solo portfolio project, but issues and discussions are welcome. Once the architecture stabilizes after Phase 1, contribution guidelines will be published.

## License

Licensed under the [Apache License, Version 2.0](LICENSE). See [`NOTICE`](NOTICE) for attribution.

## Acknowledgements

- ASAM e.V. for the XCP specification
- Raspberry Pi Foundation for the Pico SDK and excellent documentation
- The Vector Informatik documentation, which sets the gold standard for explaining XCP to newcomers