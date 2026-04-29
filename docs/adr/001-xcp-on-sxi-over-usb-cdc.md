# ADR-001: XCP on SxI framing over USB CDC

- **Status:** Accepted
- **Date:** 2026-04-29
- **Deciders:** Saeid Zoka

## Context

The XCP (Universal Measurement and Calibration Protocol, ASAM MCD-1) standard defines several "transport layer bindings" that specify how XCP packets are carried over a physical or logical transport. The standard ones are:

- **XCP on CAN** (most common in automotive)
- **XCP on Ethernet** (TCP and UDP variants)
- **XCP on FlexRay**
- **XCP on USB**
- **XCP on SxI** (Serial Interface, designed for UART/SPI byte streams)

For Phase 1 of `xcp-pico`, the development transport is USB, because:

1. The Raspberry Pi Pico 2 has native USB support with no external hardware required.
2. USB allows fast iteration during early development without needing a CAN transceiver and bus.
3. USB CDC (Communications Device Class) presents itself as a virtual serial port on the host, which is trivial to access from Python via `pyserial`.

The natural question is: **why not just implement "XCP on USB" as defined in the standard?**

Reading the ASAM XCP V1.5 Part 1 specification reveals an explicit statement that "XCP on USB has no practical significance." The spec does describe a USB binding, but in practice the automotive industry never adopted it. There are no commercial XCP masters that meaningfully support XCP on USB, and the binding requires custom USB descriptors and class-specific control transfers that complicate both the slave (firmware) and master (PC tool) implementations.

Additionally, building a full custom USB class for a portfolio project would consume significant engineering effort that adds no real-world value, since:

- No commercial XCP master tool implements this binding.
- The complexity (USB descriptors, endpoints, control transfers) does not generalize to other transports.
- It would tie us to USB specifically, making future migration to UART, SPI, or CAN harder.

## Decision

**We will implement XCP on SxI framing carried over a USB CDC byte stream.**

Concretely:

- The host sees the Pico 2 as a standard USB CDC virtual serial port. No custom USB drivers or descriptors are needed beyond what `tinyusb` provides out of the box.
- The transport layer of `xcp-pico` reads raw bytes from the CDC endpoint and applies the **XCP on SxI framing** defined in the ASAM XCP V1.5 specification, Part 3.
- The SxI frame structure is `[LEN (2 bytes)] [CTR (2 bytes)] [XCP packet (LEN bytes)]`, in little-endian.
- `LEN` is the length of the XCP packet (not including the header itself).
- `CTR` is a 16-bit counter that increments per packet, used to detect lost or out-of-order frames.

The protocol layer above the transport is **completely unaware** that the underlying transport is USB CDC. It only sees complete XCP packets.

## Consequences

### Positive

- **Transport portability.** The protocol layer can be reused unchanged when we add UART or SPI transports in the future. SxI framing is the same whether the byte stream comes from USB CDC, a UART peripheral, or an SPI slave.
- **No custom USB stack needed.** Standard CDC is well-supported by `tinyusb` (which ships with the Pico SDK) and trivially accessible from Python on the host.
- **Spec compliance.** SxI is a real, documented ASAM binding. We are not inventing a custom protocol on top of XCP. Any engineer familiar with XCP will recognize the framing immediately.
- **Easier debugging.** Because the host sees a standard serial port, raw bytes can be inspected with any serial terminal, and Python loopback tests are trivial.

### Negative

- **Master tool incompatibility.** No commercial XCP master will work with this slave out of the box, because they expect either CAN, Ethernet, or (rarely) the standard USB binding. We must write our own master in the PC tool. This is acceptable because writing a master is part of the project scope anyway.
- **Slight overhead.** SxI framing adds 4 bytes per packet (LEN + CTR). Negligible at USB CDC speeds.
- **CDC quirks on Windows.** USB CDC has well-known driver quirks on Windows (COM port enumeration, latency timer settings). These are operational nuisances rather than architectural problems.

### Neutral

- The Phase 2 CAN transport will not use SxI framing. CAN has its own XCP binding (`XCP on CAN`) which uses CAN frames directly. This means the transport layer interface has to be designed carefully so that both SxI and CAN bindings can fit behind the same abstraction. This is a follow-up concern for the transport layer module design, not a problem with this decision.

## Alternatives considered

### Alternative 1: Implement XCP on USB as defined in the standard

**Rejected** because:

- The ASAM spec itself flags this binding as having no practical significance.
- No commercial XCP master implements it, so we'd be building a slave that no one can talk to except our own master, which gives us none of the spec-compliance benefits.
- It requires custom USB class descriptors and control transfers, adding firmware complexity for no architectural gain.

### Alternative 2: Define a custom framing protocol over USB CDC

**Rejected** because:

- Inventing a custom protocol when a standard one (SxI) fits perfectly is bad engineering.
- A custom framing would not be recognizable to anyone reviewing the code, weakening the project's positioning as a spec-compliant XCP implementation.

### Alternative 3: Skip USB and start directly with CAN

**Rejected** for Phase 1 because:

- Adds hardware complexity (CAN transceiver, bus termination, second device or analyzer) before the protocol and application layers are even working.
- Slows down the development feedback loop.
- The whole point of having transport portability is that we can start simple and add CAN later. Starting with USB validates that portability claim.

CAN remains the goal for Phase 2.

## References

- ASAM XCP V1.5 Part 1: Overview
- ASAM XCP V1.5 Part 3: Transport Layer Specifications (SxI section)
- Vector Informatik XCP Reference Book V3.0